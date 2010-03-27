<?php
// $Id$

// Copyright 2008-2009 Jonathan Brown


function _storage_copy_candidate_containers($storage, $container) {

  // Find all the containers in $storage's class that are using $container's service, 
  // except $container .

  $query = db_select('storage_selector', NULL, array('fetch' => PDO::FETCH_ASSOC));
  $query->join('storage_class_container', NULL, 'storage_selector.class_id = storage_class_container.class_id');
  $query->join('storage_container', NULL, 'storage_class_container.container_id = storage_container.container_id');
  
  $result = $query->fields('storage_container', array('container_id'))
    ->condition('storage_selector.selector_id', $storage['selector_id'])
    ->condition('storage_class_container.container_id', $container['container_id'], '!=')
    ->condition('storage_container}.service_id', $container['service_id'])
    ->orderBy('storage_class_container.weight')
    ->orderBy('storage_container.container_id')
    ->execute();
  
  $candiate_container_ids = array();
  
  foreach($result as $candiate_container) {
    $candiate_container_ids[] = $candiate_container['container_id'];
  }
  
  return $candiate_container_ids;
}


function _storage_try_instance_copy($container, $storage) {
  
  $candiate_container_ids = _storage_copy_candidate_containers($storage, $container);
  
  foreach($candiate_container_ids as $candiate_container_id) {

    if(_storage_instance_exists($storage['file_id'], $candiate_container_id)) {
      $source_container = storage_container_load($candiate_container_id);
      return _storage_instance_copy($container, $storage, $source_container);
    }
  }
}


function _storage_ensure_instance_exists(&$storage, $container) {

  if($container['suspended'] != 0 ||
    $container['service_suspended'] != 0)
  {
    return FALSE;
  }
  
  $check = FALSE;

  if($storage['file_id']) {
    
    if(_storage_instance_exists($storage['file_id'], $container['container_id']))
      return TRUE;
  }
  else {
  
    // we don't have an file_id yet, so we might check for an instance once we do
  
    $check = TRUE;
  }
  
  // is it possible to short circuit?

  $service_info = _storage_service_invoke($container['service_id'], 'service_info');

  if($service_info['can_copy'] && !$check) {
  
    if(_storage_try_instance_copy($container, $storage))
      return TRUE;
  }

  $filepath = storage_get_local_filepath($storage, $keep_me);
  
  $exists = FALSE;
  
  if($filepath) {
  
    // if it's a new file, there is no point in checking for an instance
  
    if($check && !$storage['new_file']) {
      $exists = _storage_instance_exists($storage['file_id'], $container['container_id']);
    }
    
    if(!$exists) {
      $storage['filepath'] = $filepath;
      $storage['keep_me'] = $keep_me;
      
      $exists = _storage_create_instance($container, $storage);
    }
    
    if(!$keep_me) {
      @unlink($filepath);
    }
  }
  
  return $exists;
}


/**
 * Ensure there is an instance of the file in each container
 */
function _storage_ensure_all_instances_exist(&$storage, $containers) {
  $failure = FALSE;

  foreach($containers as $container) {

    $success = _storage_ensure_instance_exists($storage, $container);
    
    if(!$success)
      $failure = TRUE;
  }  
  
  return !$failure;
}


function _storage_instance_destroy_if_unrequired($container, $storage) {
  $failure = FALSE;

  // are there any other files in this container that use the same file?
  
  // we don't destroy file instances in a container in a class that a file is migrating away from
  // because that instance might not have been migrated yet 
  // - so there is no {storage_selector}.migrating = 0 condition

  $query = db_select('storage');
  $query->join('storage_selector', NULL, 'storage.selector_id = storage_selector.selector_id');
  $query->join('storage_class_container', NULL, 'storage_selector.class_id = storage_class_container.class_id');

  $count = $query->condition('storage.storage_id', $storage['storage_id'], '!=')
    ->condition('storage.file_id', $storage['file_id'])
    ->condition('storage_class_container.container_id', $container['container_id'])
    ->countQuery()
    ->execute()
    ->fetchField();
  
  if($count == 0) {
    $success = _storage_instance_destroy($container, $storage);
    
    if(!$success)
      $failure = TRUE;
  }
  
  return !$failure;
}


function _storage_destroy_unrequired_instances($storage, $container_ids) {
  $failure = FALSE;
  
  // destroy unrequired file instances
  
  $query = db_select('storage_instance', NULL, array('fetch' => PDO::FETCH_ASSOC));
  $query->join('storage_container', NULL, 'storage_instance.container_id = storage_container.container_id');
  $query->join('storage_service', NULL, 'storage_container.service_id = storage_service.service_id');

  $query->fields('storage_container')
    ->fields('storage_instance', array('reference'))
    ->condition('storage_instance.file_id', $storage['file_id'])
    ->orderBy('storage_container.container_id');
    
  $query->addField('storage_service', 'suspended', 'service_suspended');
  
  if(count($container_ids))
    $query->condition('storage_instance.container_id', $container_ids, 'NOT IN');
    
  $result = $query->execute();

  foreach($result as $container) {
    $container['settings'] = unserialize($container['settings']);
    $success = _storage_instance_destroy_if_unrequired($container, $storage);
    
    if(!$success)
      $failure = TRUE;
  }
  
  return !$failure;
}


function _storage_check_file($storage) {

  // get all containers this file is supposed to be in

  $query = db_select('storage', NULL, array('fetch' => PDO::FETCH_ASSOC));
  $query->join('storage_selector', NULL, 'storage.selector_id = storage_selector.selector_id');
  $query->join('storage_class', NULL, 'storage_selector.class_id = storage_class.class_id');
  $query->join('storage_class_container', NULL, 'storage_class.class_id = storage_class_container.class_id');
  $query->join('storage_container', NULL, 'storage_class_container.container_id = storage_container.container_id');
  $query->join('storage_service', NULL, 'storage_container.service_id = storage_service.service_id');
  
  $query->fields('storage_container');
  $query->addField('storage_service', 'suspended', 'service_suspended');
  
  $result = $query->condition('storage.storage_id', $storage['storage_id'])
    ->condition('storage_selector.migrating', 0)
    ->orderBy('storage_class_container.weight')
    ->orderBy('storage_container.container_id')
    ->execute();
  
  $containers = array();
  $container_ids = array();
  
  foreach($result as $container) {
    $container['settings'] = unserialize($container['settings']);
    $containers[] = $container;
    $container_ids[] = $container['container_id'];
  }
  
  if(_storage_ensure_all_instances_exist($storage, $containers)) {
  
    // the file is fully propagated, now we can destroy unrequired instances
  
    $success = _storage_destroy_unrequired_instances($storage, $container_ids);
  }
  
  
  if($success) {
  
    if($storage['selector_id']) {

      $update = array(
        'storage_id' => $storage['storage_id'],
        'check_me' => FALSE,
      );
      
      drupal_write_record('storage', $update, 'storage_id');
    }
    else {
    
      // this file can now be deleted
    
      db_delete('storage')
        ->condition('storage_id', $storage['storage_id'])
        ->execute();

      _storage_delete_file_if_unrequired($storage['file_id']);
    }
  }
}


function _storage_check_files() {

  $result = db_select('storage', NULL, array('fetch' => PDO::FETCH_ASSOC))
    ->fields('storage', array('storage_id'))
    ->condition('check_me', 1)
    ->orderBy('storage_id')
    ->execute();

  foreach($result as $storage) {
    $storage = storage_load($storage['storage_id']);
    _storage_check_file($storage);
  }
}


function _storage_check_migrations() {

  // find selectors that are being migrated

  $result = db_select('storage_selector', NULL, array('fetch' => PDO::FETCH_ASSOC))
    ->fields('storage_selector', array('selector_id'))
    ->distinct()
    ->condition('migrating', 1)
    ->orderBy('selector_id')
    ->execute();
 
  foreach($result as $selector) {
  
    // is the selector fully migrated?
    
    $migrated = !db_select('storage')
      ->condition('selector_id', $selector['selector_id'])
      ->condition('check_me', 1)
      ->countQuery()
      ->execute()
      ->fetchField();

    if($migrated) {
    
      // remove the classes it was migrating from
        
      db_delete('storage_selector')
        ->condition('selector_id', $selector['selector_id'])
        ->condition('migrating', '1')
        ->execute();
        
      $query = db_select('storage_selector');
      $query->join('storage_class', NULL, 'storage_selector.class_id = storage_class.class_id');
      
      $class_name = $query->fields('storage_class', array('name'))
        ->condition('storage_selector.selector_id', $selector['selector_id'])
        ->execute()
        ->fetchField();
      
      $message = 'Storage selector ' . $selector['selector_id'];
      $message .= ' has been fully migrated to class <i>' . $class_name . '</i>.';
      
      watchdog('storage_api', $message, NULL);
    }
  }
}


function _storage_flush_storage_servings() {

  db_delete('storage_serving')
    ->condition('timestamp', REQUEST_TIME - variable_get('statistics_flush_accesslog_timer', 259200), '<')
    ->execute();
}


function _storage_api_cron() {

  _storage_check_files();
  _storage_check_migrations();
  _storage_flush_storage_servings();
}

