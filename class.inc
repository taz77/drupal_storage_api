<?php
// $Id$

// Copyright 2010 Jonathan Brown

class StorageClass {

  function __construct($class) {
  
    if (!is_array($class)) {
    
      $class = db_select('storage_class')
        ->fields('storage_class')
        ->condition('class_id', $class)
        ->execute()
        ->fetchAssoc();

      if(!$class)
        throw new Exception();
    }
    
    foreach ($class as $key => $value)
      $this->$key = $value;
      
    $this->options = unserialize($this->options);
    
    try {
      if(isset($this->options['initial_container_id']))
        $this->initial_container = new StorageContainer($this->options['initial_container_id']);
    }
    catch (Exception $e) {}
  }

  function name() {
    return check_plain($this->name);
  }
  
  function path() {
    return 'admin/structure/storage/classes/' . $this->class_id;
  }
  
  function link() {
    return l($this->name, $this->path());
  }
  
  function addContainer($container) {
  
    db_insert('storage_class_container')
      ->fields(array(
        'class_id' => $this->class_id,
        'container_id' => $container->container_id,
        'serving' => TRUE,
      ))
      ->execute();  
    
    if (empty($this->options['initial_container_id'])) {
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

  function removeContainer($container) {

    db_delete('storage_class_container')
      ->condition('class_id', $this->class_id)
      ->condition('container_id', $container['container_id'])
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

    $container = storage_container_load($container['container_id']);
    
    $message = 'Storage container <i>' . $container['name'] . 
      '</i> has been removed from class <i>' . $this->name .'</i>.';
      
    drupal_set_message($message);
    watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $this->path()));
  }
  
  function update() {
    drupal_write_record('storage_class', $this, 'class_id');
  }

  function destroy() {

    db_delete('storage_class')
      ->condition('class_id', $this->class_id)
      ->execute();

    $message = 'Storage class <i>' . $this->name . '</i> has been destroyed.';

    drupal_set_message($message);
    watchdog('storage_api', $message, NULL);
  }
}


/**
 * Create a storage class.
 *
 * @param $class
 *   Class to be created.
 */
function storage_class_create($name, $options = array()) {

  $class = array(
    'name' => $name,
    'options' => $options,
  );

  drupal_write_record('storage_class', $class);
  $class = new StorageClass($class['class_id']);
  
  $msg = 'Storage class !class has been created.';
  drupal_set_message(t($msg, array('!class' => $class->link())));
  watchdog('storage_api', $msg, array('!class' => $class->name()), WATCHDOG_NOTICE, 
    l(t('view'), $class->path()));
    
  return $class;
}

