<?php

// Copyright 2010-2011 Jonathan Brown

class StorageClass {

  public $name;
  public $options;

  public function __construct($class = NULL) {
  
    if (!isset($class)) {
      return;
    }
  
    if (!is_array($class)) {
    
      $class = db_select('storage_class')
        ->fields('storage_class')
        ->condition('class_id', $class)
        ->execute()
        ->fetchAssoc();

      if (!$class)
        throw new Exception();

      $class['options'] = unserialize($class['options']);
    }
    
    foreach ($class as $key => $value) {
      $this->$key = $value;
    }
      
    try {
      if (isset($this->options['initial_container_id']))
        $this->initial_container = storage_container_load($this->options['initial_container_id']);
    }
    catch (Exception $e) {}
  }

  public function name() {
    return check_plain($this->name);
  }
  
  public function path() {
    return 'admin/structure/storage/classes/' . $this->class_id;
  }
  
  public function link() {
    return l($this->name, $this->path());
  }
  
  public function addContainer(StorageContainer $container) {
  
    db_insert('storage_class_container')
      ->fields(array(
        'class_id' => $this->class_id,
        'container_id' => $container->container_id,
      ))
      ->execute();  
    
    if (empty($this->initial_container)) {
      $this->options['initial_container_id'] = $container->container_id;
      $this->update();
    }

    $selector_subquery = db_select('storage_selector')
      ->fields('storage_selector', array('selector_id'))
      ->condition('class_id', $this->class_id);
      
    db_update('storage')
      ->fields(array('check_me' => 1))
      ->expression('serving_container', 'NULL')
      ->condition('selector_id', $selector_subquery, 'IN')
      ->execute();
    
    $msg = 'Storage container !container has been added to class !class.';
    $args = array(
      '!container' => $container->link(), 
      '!class' => $this->link(),
    );
    
    drupal_set_message(t($msg, $args));
    watchdog('storage_api', $msg, $args, WATCHDOG_NOTICE, l(t('view'), $this->path()));
  }

  /**
   * Ensure it is safe to remove the container from the class.
   */
  private function removeContainerEnsureSafe(StorageContainer $container) {

    // Get a distinct list of all file_ids of instances in other containers
    // in the class.
    $subquery = db_select('storage_class_container');
    $subquery->join('storage_instance', NULL, 'storage_class_container.container_id = storage_instance.container_id');

    $subquery->condition('storage_class_container.class_id', $this->class_id)
      ->condition('storage_class_container.container_id', $container->container_id, '!=')
      ->fields('storage_instance', array('file_id'))
      ->distinct();

    // How many instances are there of storages in this class that are in
    // the container to be removed, but are not in another container in the
    // class.
    $query = db_select('storage_selector');
    $query->join('storage', NULL, 'storage_selector.selector_id = storage.selector_id');
    $query->join('storage_instance', NULL, 'storage.file_id = storage_instance.file_id');

    $count = $query->condition('storage_selector.class_id', $this->class_id)
      ->condition('storage_instance.container_id', $container->container_id)
      ->condition('storage_instance.file_id', $subquery, 'NOT IN')
      ->countQuery()
      ->execute()
      ->fetchField();

    // If any data-loss would occur, abort.
    if ($count > 0) {
      throw new Exception();
    }
  }

  public function removeContainer(StorageContainer $container) {

    // Ensure it is safe to remove the container from the class.
    $this->removeContainerEnsureSafe($container);

    db_delete('storage_class_container')
      ->condition('class_id', $this->class_id)
      ->condition('container_id', $container->container_id)
      ->execute();

    $selector_subquery = db_select('storage_selector')
      ->fields('storage_selector', array('selector_id'))
      ->condition('class_id', $this->class_id);

    db_update('storage')
      ->fields(array('check_me' => 1))
      ->expression('serving_container', 'NULL')
      ->condition('selector_id', $selector_subquery, 'IN')
      ->execute();

    cache_clear_all();

    $message = 'Storage container <i>' . $container->name() . 
      '</i> has been removed from class <i>' . $this->name() .'</i>.';
      
    drupal_set_message($message);
    watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $this->path()));
  }
  
  public function insert() {
    drupal_write_record('storage_class', $this);
  }

  public function update() {
    drupal_write_record('storage_class', $this, 'class_id');
  }
  
  public function create() {
    $this->insert();
    
    $msg = 'Storage class !class has been created.';
    drupal_set_message(t($msg, array('!class' => $this->link())));
    watchdog('storage_api', $msg, array('!class' => $this->name()), WATCHDOG_NOTICE, 
      l(t('view'), $this->path()));
  }

  public function delete() {

    db_delete('storage_class')
      ->condition('class_id', $this->class_id)
      ->execute();

    db_delete('storage_class_container')
      ->condition('class_id', $this->class_id)
      ->execute();

    $message = 'Storage class <i>' . $this->name . '</i> has been deleted.';

    drupal_set_message($message);
    watchdog('storage_api', $message, NULL);
  }

  public function formElements() {

    $form['name'] = array(
      '#title' => t('Name'),
      '#type' => 'textfield',
      '#required' => TRUE,
      '#default_value' => $this->name,
    );
    
    $form['options'] = array(
      '#tree' => TRUE,
      'redirect' => array(
        '#title' => t('Serve by redirection'),
        '#type' => 'checkbox',
        '#default_value' => isset($this->options['redirect']) ? $this->options['redirect'] : FALSE,
      ),
      'serve_source_url' => array(
        '#title' => t('Serve source url'),
        '#type' => 'checkbox',
        '#default_value' => isset($this->options['serve_source_url']) ? $this->options['serve_source_url'] : FALSE,
        '#description' => t('If no instances are available, serve the source url.'),
      )
    );

    return $form;
  }

  public function selectorCount() {

    return db_select('storage_selector')
      ->condition('class_id', $this->class_id)
      ->countQuery()
      ->execute()
      ->fetchField();
  }
}

