<?php
// $Id$

// Copyright 2008-2009 Jonathan Brown


function _storage_api_copy_candidate_containers($file, $container) {

  // Find all the containers in $file's class that are using $container's service, 
  // except $container .

  $query = db_select('storage_selector', NULL, array('fetch' => PDO::FETCH_ASSOC));
  $query->join('storage_class_container', NULL, 'storage_selector.class_id = storage_class_container.class_id');
  $query->join('storage_container', NULL, 'storage_class_container.container_id = storage_container.container_id');
  
  $result = $query->fields('storage_container', array('container_id'))
    ->condition('storage_selector.selector_id', $file['selector_id'])
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


function _storage_api_try_object_instance_copy($container, $file) {
  
  $candiate_container_ids = _storage_api_copy_candidate_containers($file, $container);
  
  foreach($candiate_container_ids as $candiate_container_id) {

    if(_storage_api_object_instance_exists($file['object_id'], $candiate_container_id)) {
      $source_container = storage_api_container_load($candiate_container_id);
      return _storage_api_object_instance_copy($container, $file, $source_container);
    }
  }
}


function _storage_api_try_file_instance_copy($container, $file) {
  
  $candiate_container_ids = _storage_api_copy_candidate_containers($file, $container);
  
  foreach($candiate_container_ids as $candiate_container_id) {

    if(_storage_api_file_instance_exists($file['file_id'], $candiate_container_id)) {
      $source_container = storage_api_container_load($candiate_container_id);
      return _storage_api_file_instance_copy($container, $file, $source_container);
    }
  }
}


function _storage_api_ensure_instance_exists(&$file, $container) {

  if($container['suspended'] != 0 ||
    $container['service_suspended'] != 0)
  {
    return FALSE;
  }
  
  $service_info = _storage_api_service_invoke($container['service_id'], 'service_info');


  // should the instance be an object instance or a file instance 

  if(!(!$service_info['dynamic'] && $container['serving'] && $file['force_download'])) {
  
  
    // object instance

    if($file['object_id']) {
      
      if(_storage_api_object_instance_exists($file['object_id'], $container['container_id']))
        return TRUE;
    }
    else {
    
      // we don't have an object_id yet, so we might check for an instance once we do
    
      $check = TRUE;
    }
    
    // is it possible to short circuit?

    if($service_info['can_copy'] && !$check) {
    
      if(_storage_api_try_object_instance_copy($container, $file))
        return TRUE;
    }

    $filepath = storage_api_get_local_filepath($file, $keep_me);
    
    if($filepath) {
    
      // if it's a new object, there is no point in checking for an instance
    
      if($check && !$file['new_object']) {
        $exists = _storage_api_object_instance_exists($file['object_id'], $container['container_id']);
      }
      
      if(!$exists) {
        $file['filepath'] = $filepath;
        $file['keep_me'] = $keep_me;
        
        $exists = _storage_api_create_object_instance($container, $file);
      }
      
      if(!$keep_me)
        @unlink($filepath);
    }
  }
  else {
  
  
    // file instance
  
    if(_storage_api_file_instance_exists($file['file_id'], $container['container_id']))
      return TRUE;
      
    // is it possible to short circuit?
    
    if($service_info['can_copy']) {
    
      if(_storage_api_try_file_instance_copy($container, $file))
        return TRUE;
    }

    $filepath = storage_api_get_local_filepath($file, $keep_me);
    
    if($filepath) {
      $file['filepath'] = $filepath;
      $file['keep_me'] = $keep_me;
    
      $exists = _storage_api_create_file_instance($container, $file);
        
      if(!$keep_me)
        @unlink($filepath);
    }
  }
  
  return $exists;
}


/**
 * Ensure there is an instance of the file in each container
 */
function _storage_api_ensure_all_instances_exist(&$file, $containers) {

  foreach($containers as $container) {

    $success = _storage_api_ensure_instance_exists($file, $container);
    
    if(!$success)
      $failure = TRUE;
  }  
  
  return !$failure;
}


function _storage_api_object_instance_destroy_if_unrequired($container, $file) {

  // are there any other files in this container that use the same object?
  
  // we don't destroy object instances in a container in a class that a file is migrating away from
  // because that instance might not have been migrated yet 
  // - so there is no {storage_selector}.migrating = 0 condition

  $query = db_select('storage_file');
  $query->join('storage_selector', NULL, 'storage_file.selector_id = storage_selector.selector_id');
  $query->join('storage_class_container', NULL, 'storage_selector.class_id = storage_class_container.class_id');

  $files = $query->condition('storage_file.file_id', $file['file_id'], '!=')
    ->condition('storage_file.object_id', $file['object_id'])
    ->condition('storage_class_container.container_id', $container['container_id'])
    ->countQuery()
    ->execute()
    ->fetchField();
  
  if($files == 0) {
    $success = _storage_api_object_instance_destroy($container, $file);
    
    if(!$success)
      $failure = TRUE;
  }
  
  return !$failure;
}


function _storage_api_destroy_unrequired_instances($file, $container_ids) {

  // destroy unrequired object instances
  
  $query = db_select('storage_object_instance', NULL, array('fetch' => PDO::FETCH_ASSOC));
  $query->join('storage_container', NULL, 'storage_object_instance.container_id = storage_container.container_id');
  $query->join('storage_service', NULL, 'storage_container.service_id = storage_service.service_id');

  $query->fields('storage_container')
    ->fields('storage_object_instance', array('reference'))
    ->condition('storage_object_instance.object_id', $file['object_id'])
    ->orderBy('storage_container.container_id');
    
  $query->addField('storage_service', 'suspended', 'service_suspended');
  
  if($count($container_ids))
    $query->condition('storage_object_instance.container_id', $container_ids, 'NOT IN');
    
  $result = $query->execute();

  foreach($result as $container) {
    $container['settings'] = unserialize($container['settings']);
    $success = _storage_api_object_instance_destroy_if_unrequired($container, $file);
    
    if(!$success)
      $failure = TRUE;
  }


  // destroy unrequired file instances

  $query = db_select('storage_file_instance', NULL, array('fetch' => PDO::FETCH_ASSOC));
  $query->join('storage_container', NULL, 'storage_file_instance.container_id = storage_container.container_id');
  $query->join('storage_service', NULL, 'storage_container.service_id = storage_service.service_id');

  $query->fields('storage_container')
    ->fields('storage_file_instance', array('reference'))
    ->condition('storage_file_instance.file_id', $file['file_id'])
    ->orderBy('storage_container.container_id');
    
  $query->addField('storage_service', 'suspended', 'service_suspended');
  
  if($count($container_ids))
    $query->condition('storage_file_instance.container_id', $container_ids, 'NOT IN');
    
  $result = $query->execute();

  foreach($result as $container) {
    $container['settings'] = unserialize($container['settings']);
    $success = _storage_api_file_instance_destroy($container, $file);
    
    if(!$success)
      $failure = TRUE;
  }
  
  return !$failure;
}


function _storage_api_check_file($file) {

  // get all containers this file is supposed to be in

  $query = db_select('storage_file', NULL, array('fetch' => PDO::FETCH_ASSOC));
  $query->join('storage_selector', NULL, 'storage_file.selector_id = storage_selector.selector_id');
  $query->join('storage_class', NULL, 'storage_selector.class_id = storage_class.class_id');
  $query->join('storage_class_container', NULL, 'storage_class.class_id = storage_class_container.class_id');
  $query->join('storage_container', NULL, 'storage_class_container.container_id = storage_container.container_id');
  $query->join('storage_service', NULL, 'storage_container.service_id = storage_service.service_id');
  
  $query->fields('storage_container');
  $query->addField('storage_service', 'suspended', 'service_suspended');
  
  $result = $query->condition('storage_file.file_id', $file['file_id'])
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
  
  if(_storage_api_ensure_all_instances_exist($file, $containers)) {
  
    // the file is fully propagated, now we can destroy unrequired instances
  
    $success = _storage_api_destroy_unrequired_instances($file, $container_ids);
  }
  
  
  if($success) {
  
    if($file['selector_id']) {

      $update = array(
        'file_id' => $file['file_id'],
        'check_me' => FALSE,
      );
      
      drupal_write_record('storage_file', $update, 'file_id');
    }
    else {
    
      // this file can now be deleted
    
      db_delete('storage_file')
        ->condition('file_id', $file['file_id'])
        ->execute();

      _storage_api_delete_object_if_unrequired($file['object_id']);
    }
  }
}


function _storage_api_check_files() {

  $result = db_select('storage_file', NULL, array('fetch' => PDO::FETCH_ASSOC))
    ->fields('storage_file', array('file_id'))
    ->condition('check_me', 1)
    ->orderBy('file_id')
    ->execute();

  foreach($result as $file) {
    $file = storage_api_file_load($file['file_id']);
    _storage_api_check_file($file);
  }
}


function _storage_api_check_migrations() {

  // find selectors that are being migrated

  $result = db_select('storage_selector', NULL, array('fetch' => PDO::FETCH_ASSOC))
    ->fields('storage_selector', array('selector_id'))
    ->distinct()
    ->condition('migrating', 1)
    ->orderBy('selector_id')
    ->execute();
 
  foreach($result as $selector) {
  
    // is the selector fully migrated?
    
    $migrated = !db_select('storage_file')
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


function _storage_api_flush_storage_servings() {

  db_delete('storage_serving')
    ->condition('timestamp', REQUEST_TIME - variable_get('statistics_flush_accesslog_timer', 259200), '<')
    ->execute();
}


/**
 * Implement hook_cron() .
 */
function storage_api_cron() {

  _storage_api_check_files();
  _storage_api_check_migrations();
  _storage_api_flush_storage_servings();
}

