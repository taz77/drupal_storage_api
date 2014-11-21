<?php

/**
 * @file
 * Hooks provided by Storage.
 */
/**
 * @addtogroup hooks
 * @{
 */

/**
 * Generate a file.
 *
 * @param $storage
 *   The storage to be generated.
 * @return
 *   Filepath to the generated file.
 */
function hook_storage_generate(Storage $storage) {
  
}

/**
 * Determine the current user's access to a file.
 *
 * @param $storage
 *   Storage that is attempting to be accessed.
 * @return
 *   Whether access is permitted or not.
 *   - TRUE for permitted.
 *   - FALSE for denied.
 *   - NULL to inherit the parent's access.
 */
function hook_storage_access(Storage $storage) {
  
}

/**
 * Alter the current user's access to a file that belongs to another module.
 *
 * @param $storage
 *   Storage that is attempting to be accessed.
 * @return
 *   Access alteration.
 *   - TRUE for permitted.
 *   - FALSE for denied.
 *   - NULL for don't alter.
 */
function hook_storage_access_alter(Storage $storage) {
  
}

/**
 * Provides information about the storage service.
 *
 * @return
 *   An associative array, with the following keys:
 *     - 'name'
 *       Translated name of the storage service.
 *     - 'class'
 *     - 'htaccess'
 *     - 'copy'
 *     - 'serve'
 *     - 'serve_secure'
 */
function hook_storage_service_info() {
  
}

/**
 * Allows other modules to alter options for storage objects for styled images.
 */
function hook_storage_core_bridge_styled_image_options_alter(&$options, $uri, $parent) {
  $status = mymodule_file_status_from_storage_id($parent->storage_id);

  // Set the initial container to public if the file is published.
  if ($status == MYMODULE_PUBLISHED && $container_id = variable_get('mymodule_public_id', NULL)) {
    $options['initial_container_id'] = $container_id;
  }
}
