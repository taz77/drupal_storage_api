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
 * @param \Storage $storage
 *   The storage to be generated.
 * @return string
 *   Filepath to the generated file.
 */
function hook_storage_generate(Storage $storage) {

}

/**
 * Determine the current user's access to a file.
 *
 * @param \Storage $storage
 *   Storage that is attempting to be accessed.
 * @return bool
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
 * @param \Storage $storage
 *   Storage that is attempting to be accessed.
 * @return bool
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
 * @return array
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
 *
 * @param array $options
 *    Image options
 * @param string $uri
 * @param object $parent
 *    Parent file object
 */
function hook_storage_core_bridge_styled_image_options_alter(&$options, $uri, $parent) {
  $status = mymodule_file_status_from_storage_id($parent->storage_id);

  // Set the initial container to public if the file is published.
  if ($status == MYMODULE_PUBLISHED && $container_id = variable_get('mymodule_public_id', NULL)) {
    $options['initial_container_id'] = $container_id;
  }
}

/**
 * Alter http headers for files served by storage api.
 *
 * @param array $headers
 * @param object $storage
 */
function hook_storage_http_headers_alter(&$headers, $storage) {
  if (strpos($storage->mimetype, 'image') === 0) {
    unset($headers['Content-disposition']);
  }
}

