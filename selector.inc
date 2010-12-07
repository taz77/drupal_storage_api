<?php
// $Id$

// Copyright 2010 Jonathan Brown


class StorageSelector {

  public $selectorId;

  public function __construct($arg0, $arg1 = NULL) {
    
    if (!is_null($arg1)) {
      $this->selectorId = $arg0 . '/' . $arg1;
    }
    else {
      $this->selectorId = $arg0;
    }
  }
  
  /**
   * Creates a new class selector.
   */
  public function create() {
  
    $class_id = db_select('storage_class')
      ->fields('storage_class', array('class_id'))
      ->orderBy('name')
      ->orderBy('class_id')
      ->range(0, 1)
      ->execute()
      ->fetchField();
      
    db_insert('storage_selector')
      ->fields(array(
        'selector_id' => $this->selectorId,
        'class_id' => $class_id,
      ))
      ->execute();
  }
  
  /**
   * Deletes a class selector.
   */
  public function destroy() {
  
    db_delete('storage_selector')
      ->condition('selector_id', $this->selectorId)
      ->execute();
  }
  
  /**
   * Generates a FAPI select element for a class selector.
   *
   * After form submission, pass the form value for the element to storage_selector_submit() .
   *
   * @param $title
   *   The title of the select element.
   * @param $description
   *   The description of the select element (optional).
   * @return
   *   The select element.
   */
  public function formItem($title, $description = NULL) {
    $selector = array(
      '#tree' => TRUE
    );
    
    $result = db_select('storage_class', NULL, array('fetch' => PDO::FETCH_ASSOC))
      ->fields('storage_class')
      ->orderBy('name')
      ->orderBy('class_id')
      ->execute();
    
    $classes = array();

    foreach ($result as $class) {
      $classes[$class['class_id']] = $class['name'];
    }
    
    $selector['class_id'] = array(
      '#type' => 'select',
      '#title' => $title,
      '#description' => $description,
      '#options' => $classes
    );
    
    $current_class_id = db_select('storage_selector')
      ->fields('storage_selector', array('class_id'))
      ->condition('selector_id', $this->selectorId)
      ->condition('migrating', 0)
      ->execute()
      ->fetchField();

    $selector['class_id']['#default_value'] = $current_class_id;
    
    $selector['current_class_id'] = array(
      '#type' => 'value',
      '#value' => $current_class_id
    );
    
    return $selector;
  }
  
  /**
   * Processes a class selector FAPI element.
   *
   * The element's class selector is updated to point at the class selected. Migration of files to the new class is initiated.
   *
   * @param $form_item
   *   The form value of a submitted class selector FAPI element.
   */
  public function submit(array $form_item) {

    // Has the class been changed?
    if ($form_item['current_class_id'] == $form_item['class_id']) {
      return;
    }


    db_merge('storage_selector')
      ->key(array(
        'selector_id' => $this->selectorId,
        'class_id' => $form_item['class_id'],
      ))
      ->fields(array('migrating' => 0))
      ->execute();

    
    // Are there any files to migrate?
    $num_files = db_select('storage')
      ->condition('selector_id', $this->selectorId)
      ->countQuery()
      ->execute()
      ->fetchField();
    
    if ($num_files) {

      db_merge('storage_selector')
        ->key(array(
          'selector_id' => $this->selectorId,
          'class_id' => $form_item['current_class_id'],
        ))
        ->fields(array('migrating' => 1))
        ->execute();

      db_update('storage')
        ->fields(array('check_me' => 1))
        ->expression('serving_container', 'NULL')
        ->condition('selector_id', $this->selectorId)
        ->execute();
      
      $new_class = storage_class_load($form_item['class_id']);
      
      $message = 'Migrating ' . $num_files . ' ' . format_plural($num_files, 'file', 'files');
      $message .= ' to storage class <i>' . $new_class->name() . '</i>.';
      
      drupal_set_message($message);
      watchdog('storage_api', $message, NULL);
    }
    else {
    
      db_delete('storage_selector')
        ->condition('selector_id', $this->selectorId)
        ->condition('class_id', $form_item['current_class_id'])
        ->execute();
    }
  }

  public function storageClass() {

    return storage_class_load(db_select('storage_selector')
      ->fields('storage_selector', array('class_id'))
      ->condition('selector_id', $this->selectorId)
      ->condition('migrating', 0)
      ->execute()
      ->fetchField());
  }
  
  public function initialContainer() {

    return $this->storageClass()->initial_container;
  }
  
  public function checkMigration() {
  
    // Is the selector fully migrated?
    $migrated = !db_select('storage')
      ->condition('selector_id', $this->selectorId)
      ->condition('check_me', 1)
      ->countQuery()
      ->execute()
      ->fetchField();

    if ($migrated) {
    
      // Remove the classes it was migrating from.
      db_delete('storage_selector')
        ->condition('selector_id', $this->selectorId)
        ->condition('migrating', '1')
        ->execute();
        
      $message = 'Storage selector ' . check_plain($this->selectorId);
      $message .= ' has been fully migrated to class <i>' . $this->storageClass()->name() . '</i>.';
      
      watchdog('storage_api', $message, NULL);
    }
  }
}

