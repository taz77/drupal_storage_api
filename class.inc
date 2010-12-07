<?php
// $Id$

// Copyright 2010 Jonathan Brown

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
        'serving' => TRUE,
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

  public function removeContainer(StorageContainer $container) {

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

  public function destroy() {

    db_delete('storage_class')
      ->condition('class_id', $this->class_id)
      ->execute();

    $message = 'Storage class <i>' . $this->name . '</i> has been destroyed.';

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
      'access_control' => array(
        '#title' => t('Enforce access control'),
        '#type' => 'checkbox',
        '#default_value' => isset($this->options['access_control']) ? $this->options['access_control'] : FALSE,
      ),
      'redirect' => array(
        '#title' => t('Serve by redirection'),
        '#type' => 'checkbox',
        '#default_value' => isset($this->options['redirect']) ? $this->options['redirect'] : FALSE,
      ),
      'serve_source_url' => array(
        '#title' => t('Serve source url'),
        '#type' => 'checkbox',
        '#default_value' => isset($this->options['serve_source_url']) ? $this->options['serve_source_url'] : FALSE,
        '#description' => t('If no instances are available, serve the source url.')
      )
    );

    return $form;
  }  
}

