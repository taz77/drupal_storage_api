<?php

// Copyright 2010-2011 Jonathan Brown


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

    db_update('storage')
      ->fields(array(
        'check_me' => 1,
        'serving_container' => serialize(FALSE),
      ))
      ->expression('selector_id', 'NULL')
      ->condition('selector_id', $this->selectorId)
      ->execute();

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
  public function formItem($title = NULL, $description = NULL) {
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

    if (!$description) {
      $text = "This can be safely changed at any time.<br />";
      $text .= "Configure storage classes <a href=\"!path\">here</a>.";

      $description = t($text, array('!path' => url('admin/structure/storage/classes')));
    }

    $selector['class_id'] = array(
      '#type' => 'select',
      '#title' => $title ? $title : t("Storage class"),
      '#description' => t($description, array('!path' => url('admin/structure/storage/classes'))),
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

  /**
   * Adds a storage to the selector.
   *
   * If the selector's class has an initial container, then an instance of the
   * storage will be created there.
   *
   * @param $options
   *   An associative array of additional options, with the following keys:
   *     - 'source_uri'
   *       URI to get the file from.
   *     - 'keep_source_uri'
   *       source_uri will be stored in the database.
   *     - 'file_id'
   *       If the same file is already in the system, its file_id can be
   *       specified.
   *     - 'filename'
   *       The file's filename.
   *     - 'module'
   *       The Drupal module that 'owns' this file.
   *     - 'type'
   *       Module-specific file type identifier.
   *     - 'nid'
   *       The nid of the node this file should be associated with.
   *     - 'created' (default current time)
   *       Unix timestamp of when the storage was created.
   *     - 'data'
   *       An array of data about the file.
   *
   * @return
   *   Storage of the added file.
   *
   * @throws StorageException
   *   When it isn't possible to add the storage to the selector.
   */
  public function storageAdd($options = array()) {

    $options += array(
      'selector_id' => $this->selectorId,
      'keep_source_uri' => FALSE,
      'created' => time(),
      'data' => array(),
    );

    $initial_container = $this->storageClass()->initial_container;

    if ($initial_container) {
      $options['initial_container_id'] = $initial_container->container_id;
    }

    if (isset($options['source_uri'])) {

      if ($initial_container) {
        $source_uri = $options['source_uri'];   // TODO: Make this URI local.
        $filename = isset($options['filename']) ? $options['filename'] : basename($options['source_uri']);
        $options['file_id'] = _storage_api_file_id($source_uri, $filename, $new_file);
      }

      if (!$options['keep_source_uri']) {
        unset($options['source_uri']);
      }
    }
    elseif (isset($options['filename'])) {
      // Store the filename so it can be in the generator URL.
      $options['data']['filename'] = $options['filename'];
    }

    drupal_write_record('storage', $options);
    $storage = storage_load($options['storage_id']);

    $link = $storage->nid ? l(t('view'), 'node/' . $storage->nid) : NULL;

    if (isset($source_uri)) {
      $exists = FALSE;

      // If the storage's file is already in the system, then the instance
      // might already exist.
      if (!$new_file) {
        $exists = $initial_container->instanceExists($storage);
      }

      if (!$exists) {
        try {
          $initial_container->instanceCreate($storage, $source_uri);
        }
        catch (StorageException $e) {
          $message = 'Failed to create initial instance of file ' . $storage->filename;
          $message .= ' in storage container <i>' . $initial_container->name() . '</i>.';

          if (user_access('administer storage')) {
            drupal_set_message($message);
          }

          $message .= ' <br />' . $storage->logInfo();
          watchdog('storage_api', $message, NULL, WATCHDOG_WARNING, $link);

          $storage->remove();
          throw new StorageException();
        }
      }
    }

    $message = 'File added: ' . $storage->filename . '<br />';
    $message .= $storage->logInfo();

    watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, $link);

    return $storage;
  }
}

