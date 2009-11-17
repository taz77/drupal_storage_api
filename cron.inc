<?php
// $Id$

// Copyright 2008-2009 Jonathan Brown


function _storage_api_object_instance_copy(&$container, $file, $source_container) {

  $success = _storage_api_service_invoke($container['service_id'], 'object_instance_copy', 
    $container, $file, $source_container);
  
  if(!$success)
    return FALSE;
  
  if($success !== TRUE)
    $container['reference'] = $success;
  
  $instance = array(
    'object_id' => $file['object_id'],
    'container_id' => $container['container_id'],
    'reference' => $container['reference'],
  );

  drupal_write_record('storage_object_instance', $instance);
  
  _storage_api_flush_serve_url($file['file_id']);
  
  $message = 'Object instance copied. <br />';
  $message .= _storage_api_container_log_info($container);
  $message .= _storage_api_object_log_info($file);
  
  watchdog('storage_api', $message, NULL);

  return TRUE;
}


function _storage_api_file_instance_copy(&$container, $file, $source_container) {

  $success = _storage_api_service_invoke($container['service_id'], 'file_instance_copy', 
    $container, $file, $source_container);
  
  if(!$success)
    return FALSE;
    
  if($success !== TRUE)
    $container['reference'] = $success;
  
  $instance = array(
    'file_id' => $file['file_id'],
    'container_id' => $container['container_id'],
    'reference' => $container['reference'],
  );

  drupal_write_record('storage_file_instance', $instance);
  
  _storage_api_flush_serve_url($file['file_id']);
  
  $message = 'File instance copied' . ($file['filename'] ? (': ' . $file['filename'] . ' ') : '') . '. <br />';
  $message .= _storage_api_container_log_info($container);
  $message .= _storage_api_file_log_info($file);

  watchdog('storage_api', $message, NULL);

  return TRUE;
}


function _storage_api_copy_candidate_containers($file, $container) {

  // Find all the containers in $file's class that are using $container's service, 
  // except $container .

  $result = db_query("
    SELECT {storage_container}.container_id
    FROM {storage_selector}
    INNER JOIN {storage_class_container}
      ON {storage_selector}.class_id = {storage_class_container}.class_id
    INNER JOIN {storage_container}
      ON {storage_class_container}.container_id = {storage_container}.container_id
    WHERE {storage_selector}.selector_id = %d
      AND {storage_class_container}.container_id != %d
      AND {storage_container}.service_id = '%s'
    ORDER BY {storage_class_container}.weight, {storage_class_container}.container_id
  ",
    $file['selector_id'],
    $container['container_id'],
    $container['service_id']
  );
  
  $candiate_container_ids = array();
  
  while($candiate_container = db_fetch_array($result)) {
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


function _storage_api_object_instance_destroy($container, $file) {

  if($container['suspended'] != 0 ||
    $container['service_suspended'] != 0)
  {
    return FALSE;
  }

  $success = _storage_api_service_invoke($container['service_id'], 'object_instance_destroy', 
    $container, $file);
  
  if(!$success)
    return FALSE;

  db_query("
    DELETE FROM {storage_object_instance}
    WHERE object_id = %d
      AND container_id = %d
  ",
    $file['object_id'],
    $container['container_id']
  );
  
  $message = 'Object instance destroyed. <br />';
  $message .= _storage_api_container_log_info($container);
  $message .= _storage_api_object_log_info($file);
  
  watchdog('storage_api', $message, NULL);

  return TRUE;
}


function _storage_api_object_instance_destroy_if_unrequired($container, $file) {

  // are there any other files in this container that use the same object?
  
  // we don't destroy object instances in a container in a class that a file is migrating away from
  // because that instance might not have been migrated yet 
  // - so there is no {storage_selector}.migrating = 0 condition

  
  $files = db_result(db_query("
    SELECT COUNT(*)
    FROM {storage_file}
    INNER JOIN {storage_selector}
      ON {storage_file}.selector_id = {storage_selector}.selector_id
    INNER JOIN {storage_class_container}
      ON {storage_selector}.class_id = {storage_class_container}.class_id
    WHERE {storage_file}.file_id != %d
      AND {storage_file}.object_id = %d
      AND {storage_class_container}.container_id = %d
  ",
    $file['file_id'],
    $file['object_id'],
    $container['container_id']
  ));
  
  if($files == 0) {
    $success = _storage_api_object_instance_destroy($container, $file);
    
    if(!$success)
      $failure = TRUE;
  }
  
  return !$failure;
}


function _storage_api_file_instance_destroy($container, $file) {

  if($container['suspended'] != 0 ||
    $container['service_suspended'] != 0)
  {
    return FALSE;
  }

  $success = _storage_api_service_invoke($container['service_id'], 'file_instance_destroy', 
    $container, $file);
    
  if(!$success)
    return FALSE;

  db_query("
    DELETE FROM {storage_file_instance}
    WHERE file_id = %d
      AND container_id = %d
  ",
    $file['file_id'],
    $container['container_id']
  );
  
  $message = 'File instance destroyed' . ($file['filename'] ? (': ' . $file['filename'] . ' ') : '') . '. <br />';
  $message .= _storage_api_container_log_info($container);
  $message .= _storage_api_file_log_info($file);

  watchdog('storage_api', $message, NULL);

  return TRUE;
}


function _storage_api_destroy_unrequired_instances($file, $container_ids) {

  if(count($container_ids)) {
    $placeholders = implode(',', array_fill(0, count($container_ids), '%d'));
    $objects_and = "AND {storage_object_instance}.container_id NOT IN($placeholders)";
    $files_and = "AND {storage_file_instance}.container_id NOT IN($placeholders)";
  }
  
  
  // destroy unrequired object instances
  
  $result = db_query("
    SELECT {storage_container}.*, {storage_object_instance}.reference, 
      {storage_service}.suspended AS service_suspended
    FROM {storage_object_instance}
    INNER JOIN {storage_container}
      ON {storage_object_instance}.container_id = {storage_container}.container_id
    INNER JOIN {storage_service}
      ON {storage_container}.service_id = {storage_service}.service_id
    WHERE {storage_object_instance}.object_id = %d
      $objects_and
    ORDER BY {storage_container}.container_id
  ",
    array_merge(array($file['object_id']), $container_ids)
  );

  while($container = db_fetch_array($result)) {
    $container['settings'] = unserialize($container['settings']);
    $success = _storage_api_object_instance_destroy_if_unrequired($container, $file);
    
    if(!$success)
      $failure = TRUE;
  }


  // destroy unrequired file instances

  $result = db_query("
    SELECT {storage_container}.*, {storage_file_instance}.reference, 
      {storage_service}.suspended AS service_suspended
    FROM {storage_file_instance}
    INNER JOIN {storage_container}
      ON {storage_file_instance}.container_id = {storage_container}.container_id
    INNER JOIN {storage_service}
      ON {storage_container}.service_id = {storage_service}.service_id
    WHERE {storage_file_instance}.file_id = %d
      $files_and
    ORDER BY {storage_container}.container_id
  ",
    array_merge(array($file['file_id']), $container_ids)
  );  

  while($container = db_fetch_array($result)) {
    $container['settings'] = unserialize($container['settings']);
    $success = _storage_api_file_instance_destroy($container, $file);
    
    if(!$success)
      $failure = TRUE;
  }
  
  return !$failure;
}


function _storage_api_check_file($file) {

  // get all containers this file is supposed to be in

  $result = db_query("
    SELECT {storage_container}.*, {storage_service}.suspended AS service_suspended
    FROM {storage_file}
    INNER JOIN {storage_selector}
      ON {storage_file}.selector_id = {storage_selector}.selector_id
    INNER JOIN {storage_class}
      ON {storage_selector}.class_id = {storage_class}.class_id
    INNER JOIN {storage_class_container}
      ON {storage_class}.class_id = {storage_class_container}.class_id
    INNER JOIN {storage_container}
      ON {storage_class_container}.container_id = {storage_container}.container_id
    INNER JOIN {storage_service}
      ON {storage_container}.service_id = {storage_service}.service_id
    WHERE {storage_file}.file_id = %d
      AND {storage_selector}.migrating = 0
    ORDER BY {storage_class_container}.weight, {storage_container}.container_id
  ",
    $file['file_id']
  );
  
  $containers = array();
  $container_ids = array();
  
  while($container = db_fetch_array($result)) {
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
    
      db_query("
        DELETE FROM {storage_file}
        WHERE file_id = %d
      ",
        $file['file_id']
      );

      _storage_api_delete_object_if_unrequired($file['object_id']);
    }
  }
}


function _storage_api_check_files() {

  $result = db_query("
    SELECT file_id
    FROM {storage_file}
    WHERE check_me = 1
    ORDER BY file_id
  ");
  
  while($file = db_fetch_array($result)) {
    $file = storage_api_file_load($file['file_id']);
    _storage_api_check_file($file);
  }
}


function _storage_api_check_migrations() {

  // find selectors that are being migrated

  $result = db_query("
    SELECT DISTINCT selector_id
    FROM {storage_selector}
    WHERE migrating = 1
    ORDER BY selector_id
  ");
  
  
  while($selector = db_fetch_array($result)) {
  
    // is the selector fully migrated?
    
    $migrated = !db_result(db_query("
      SELECT COUNT(*)
      FROM {storage_file}
      WHERE selector_id = %d
        AND check_me = 1
    ",
      $selector['selector_id']
    ));
    

    if($migrated) {
    
      // remove the classes it was migrating from
        
      db_query("
        DELETE FROM {storage_selector}
        WHERE selector_id = %d
          AND migrating = 1
      ",
        $selector['selector_id']
      );
      
      $class_name = db_result(db_query("
        SELECT {storage_class}.name
        FROM {storage_selector}
        INNER JOIN {storage_class}
          ON {storage_selector}.class_id = {storage_class}.class_id
        WHERE {storage_selector}.selector_id = %d
      ",
        $selector['selector_id']
      ));
      
      $message = 'Storage selector ' . $selector['selector_id'];
      $message .= ' has been fully migrated to class <i>' . $class_name . '</i>.';
      
      watchdog('storage_api', $message, NULL);
    }
  }
}


function _storage_api_flush_storage_servings() {

  db_query("
    DELETE FROM {storage_serving}
    WHERE timestamp < %d
  ",
    $_SERVER['REQUEST_TIME'] - variable_get('statistics_flush_accesslog_timer', 259200)
  );
}


function _storage_api_cron() {

  _storage_api_check_files();
  _storage_api_check_migrations();
  _storage_api_flush_storage_servings();
}

