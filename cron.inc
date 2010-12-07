<?php
// $Id$

// Copyright 2008-2009 Jonathan Brown


function _storage_check_files() {

  $storage_ids = db_select('storage')
    ->fields('storage', array('storage_id'))
    ->condition('check_me', 1)
    ->orderBy('storage_id')
    ->execute()
    ->fetchCol();

  foreach ($storage_ids as $storage_id) {
    storage_load($storage_id)->check();
  }
}


function _storage_check_migrations() {

  // Find selectors that are being migrated.
  $selector_ids = db_select('storage_selector', NULL, array('fetch' => PDO::FETCH_ASSOC))
    ->fields('storage_selector', array('selector_id'))
    ->distinct()
    ->condition('migrating', 1)
    ->orderBy('selector_id')
    ->execute()
    ->fetchCol();

  foreach ($selector_ids as $selector_id) {

    storage_selector($selector_id)->checkMigration();
  }
}


function _storage_flush_storage_servings() {

  db_delete('storage_serving')
    ->condition('timestamp', REQUEST_TIME - variable_get('statistics_flush_accesslog_timer', 259200), '<')
    ->execute();
}


function _storage_api_container_maintenance() {

  $result = db_select('storage_container', NULL, array('fetch' => PDO::FETCH_ASSOC))
    ->condition('storage_container.suspended', 0)
    ->fields('storage_container')
    ->execute();

  foreach ($result as $container) {
    storage_container_new($container)->serviceMaintenance();
  }
}


function _storage_api_cron() {

  _storage_check_files();
  _storage_check_migrations();
  _storage_flush_storage_servings();
  _storage_api_container_maintenance();
}

