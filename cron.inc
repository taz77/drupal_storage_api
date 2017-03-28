<?php

/**
 * @file
 * Cron functions for Storage API.
 */

/**
 * Remove containers marked for removal.
 *
 * @throws \StorageException
 */
function _storage_remove_containers() {

  $result = db_select('storage_class_container', NULL, ['fetch' => PDO::FETCH_ASSOC])
    ->fields('storage_class_container')
    ->condition('remove', 1)
    ->execute();

  foreach ($result as $container_instance) {
    $class = storage_class_load($container_instance['class_id']);
    $container = storage_container_load($container_instance['container_id']);

    try {
      $class->removeContainer($container);
    }
    catch (StorageException $e) {
      watchdog('storage', t('Error thrown in Cron while removing containers.') . ' ' . $e, [], WATCHDOG_ERROR);
    }
  }
}

/**
 * Check that Storage can be loaded by the ID number. Throw error into logs if
 * it cannot be loaded.
 */
function _storage_check_files() {

  $result = db_select('storage')
    ->fields('storage', ['storage_id'])
    ->condition('status', STORAGE_STATUS_PROCESS_CRON)
    ->orderBy('storage_id')
    ->execute();

  while ($storage_id = $result->fetchField()) {
    try {
      storage_load($storage_id)->check();
    }
    catch (StorageException $e) {
      watchdog('storage', t('Error thrown in Cron while checking files. Please check the storage configurations. Was unable to load storage. See Error:') . ' ' . $e, [], WATCHDOG_ERROR);
    }
  }
}

/**
 * Find selectors that are being migrated and check their status.
 */
function _storage_check_migrations() {
  // Find selectors that are being migrated.
  $selector_ids = db_select('storage_selector', NULL, ['fetch' => PDO::FETCH_ASSOC])
    ->fields('storage_selector', ['selector_id'])
    ->distinct()
    ->condition('migrating', 1)
    ->orderBy('selector_id')
    ->execute()
    ->fetchCol();

  foreach ($selector_ids as $selector_id) {
    storage_selector($selector_id)->checkMigration();
  }
}

/**
 * Check active containers for their maintenance status.
 */
function _storage_container_maintenance() {

  $result = db_select('storage_container', NULL, ['fetch' => PDO::FETCH_ASSOC])
    ->condition('storage_container.status', STORAGE_CONTAINER_STATUS_ACTIVE)
    ->fields('storage_container')
    ->execute();

  foreach ($result as $container) {
    storage_container_new($container)->serviceMaintenance();
  }
}

/**
 * Destroy containers marked for destruction. Then delete the container.
 */
function _storage_destroy_containers() {

  $result = db_select('storage_container', NULL, ['fetch' => PDO::FETCH_ASSOC])
    ->condition('storage_container.status', STORAGE_CONTAINER_STATUS_DESTROY)
    ->fields('storage_container')
    ->execute();

  foreach ($result as $container) {
    try {
      $container = storage_container_new($container);
      $container->serviceDestroy();
      $container->delete();
    }
    catch (StorageException $e) {
      watchdog('storage', t('Error thrown in Cron while destroying containers.') . ' ' . $e, [], WATCHDOG_ERROR);
    }
  }
}

/**
 * Helper function to run all cron jobs.
 */
function _storage_cron() {
  _storage_remove_containers();
  _storage_check_files();
  _storage_check_migrations();
  _storage_container_maintenance();
  _storage_destroy_containers();
}
