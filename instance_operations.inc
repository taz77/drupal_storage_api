<?php
// $Id$

// Copyright 2008-2009 Jonathan Brown


function __storage_create_instance(&$container, $storage) {

  $success = _storage_service_invoke($container['service_id'], 'instance_create', 
    $container, $storage);
  
  if(!$success)
    return FALSE;
    
  if($success !== TRUE)
    $container['reference'] = $success;
  
  $instance = array(
    'file_id' => $storage['file_id'],
    'container_id' => $container['container_id'],
    'reference' => $container['reference'],
  );

  drupal_write_record('storage_instance', $instance);
  
  _storage_flush_serving_container($storage['storage_id']);
  
  $message = 'File instance created' . ($storage['filename'] ? (': ' . $storage['filename'] . ' ') : '') . '. <br />';
  $message .= _storage_container_log_info($container);
  $message .= _storage_log_info($storage);

  watchdog('storage_api', $message, NULL);

  return TRUE;
}


function _storage_create_instance(&$container, $storage) {

  // we need write access to the directory for it to be deleted

  if(!isset($storage['keep_me']) || !$storage['keep_me']) {
  
    if(!is_writable(dirname($storage['filepath'])))
      $storage['keep_me'] = TRUE;
  }
  
  // if the storage's file is already in the system, then the instance might already exist

  if(!isset($storage['new_file']) || !$storage['new_file']) {
    $instance = _storage_instance_exists($storage['file_id'], $container['container_id']);
    
    if($instance)
      $container['reference'] = $instance['reference'];
  }
  
  $failure = FALSE;
  
  if(!isset($instance) || !$instance) {
    $failure = !__storage_create_instance($container, $storage);
  }
  
  if(!$storage['keep_me'])
    @unlink($storage['filepath']);
    
  return !$failure;
}


function _storage_instance_copy(&$container, $storage, $source_container) {

  $success = _storage_service_invoke($container['service_id'], 'instance_copy', 
    $container, $storage, $source_container);
  
  if(!$success)
    return FALSE;
    
  if($success !== TRUE)
    $container['reference'] = $success;
  
  $instance = array(
    'file_id' => $storage['file_id'],
    'container_id' => $container['container_id'],
    'reference' => $container['reference'],
  );

  drupal_write_record('storage_instance', $instance);
  
  _storage_flush_serving_container($storage['storage_id']);
  
  $message = 'File instance copied' . ($storage['filename'] ? (': ' . $storage['filename'] . ' ') : '') . '. <br />';
  $message .= _storage_container_log_info($container);
  $message .= _storage_log_info($storage);

  watchdog('storage_api', $message, NULL);

  return TRUE;
}


function _storage_instance_destroy($container, $storage) {

  if($container['suspended'] != 0 ||
    $container['service_suspended'] != 0)
  {
    return FALSE;
  }

  $success = _storage_service_invoke($container['service_id'], 'instance_destroy', 
    $container, $storage);
    
  if(!$success)
    return FALSE;

  db_delete('storage_instance')
    ->condition('file_id', $storage['file_id'])
    ->condition('container_id', $container['container_id'])
    ->execute();
  
  $message = 'File instance destroyed' . ($storage['filename'] ? (': ' . $storage['filename'] . ' ') : '') . '. <br />';
  $message .= _storage_container_log_info($container);
  $message .= _storage_log_info($storage);

  watchdog('storage_api', $message, NULL);

  return TRUE;
}

