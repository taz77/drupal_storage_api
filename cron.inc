<?php
// $Id$

// Copyright 2008-2009 Jonathan Brown


function _storage_check_files() {

  $result = db_select('storage', NULL, array('fetch' => PDO::FETCH_ASSOC))
    ->fields('storage', array('storage_id'))
    ->condition('check_me', 1)
    ->orderBy('storage_id')
    ->execute();

  foreach ($result as $storage) {
    $storage = new Storage($storage['storage_id']);
    $storage->check();
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
 
  foreach ($result as $selector) {
  
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

