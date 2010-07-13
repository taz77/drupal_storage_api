<?php
// $Id$


/**
* @file
* Hooks provided by Storage API.
*/


/**
* @addtogroup hooks
* @{
*/


/**
 * Generate a file.
 *
 * @param $storage
 *   The file to be generated.
 * @return
 *   Filepath to the generated file. It will be deleted.
 */
function hook_storage_generate(Storage $storage) {
}


/**
 * Access hooks
 */


/**
 * Determine the current user's access to a file.
 *
 * @param $type
 *   Module-specific file type identifier.
 * @param $storage_id
 *   storage_id of the file.
 * @return
 *   Whether access is permitted or not.
 *   - TRUE for permitted.
 *   - FALSE for denied.
 *   - NULL for check node_access() .
 */
function hook_storage_access($type, $storage_id) {
}


/**
 * Alter the current user's access to a file that belongs to another module.
 *
 * @param $module
 *  Module that owns the file.
 * @param $type
 *  Module-specific file type identifier.
 * @param $storage_id
 *  storage_id of the file.
 * @return
 *   Access alteration.
 *   - TRUE for permitted.
 *   - FALSE for denied.
 *   - NULL for don't alter.
 */
function hook_storage_access_alter($module, $type, $storage_id) {
}


/**
 * Service hooks
 */


/**
 * Provides information about the storage service.
 *
 * @return
 *   An associative array, with the following keys:
 *     - 'name'
 *       Translated name of the storage service.
 *     - 'local'
 *       If TRUE, files returned by *_get_filepath() will not be deleted after use.
 *     - 'direct'
 *       If TRUE, the service never returns a URL from hook_storage_object_instance_serve() .
 *     - 'can_copy'
 *       Whether the service can copy an instance internally.
 *       If TRUE, *_instance_copy() should be implemented.
 */
function hook_storage_service_info() {
}


/**
 * Generate a settings form for a container.
 *
 * @param $settings
 *   
 * @param $serving
 *   Whether this is a serving container.
 * @param $edit
 *   Whether this is a container being edited.
 * @return
 *   
 */
function hook_storage_container_settings_form($settings, $serving, $edit) {
}


/**
 * Validate the settings for a storage container.
 *
 * Call form_set_error() if there are any problems.
 *
 * @param $container
 *   The container to be validated.
 * @param $edit
 *   Whether this is an existing container being edited.
 */
function hook_storage_container_validate(StorageContainer $container, $edit) {
}


/**
 * Create a storage container.
 *
 * Call form_set_error() if there are any problems.
 *
 * @param $container
 *   Container to be created.
 * @return
 *   The container (with any modifications). Even return on failure.
 */
function hook_storage_container_create(StorageContainer $container) {
}


/**
 * Respond to a container having been created.
 *
 * container_id is now available.
 *
 * @param $container
 *   Container that has been created.
 */
function hook_storage_container_postcreate(StorageContainer $container) {
}


/**
 * Update a storage container.
 *
 * @param $container
 *   Modified container.
 * @return
 *   The container (with any further modifications). Even return on failure.
 */
function hook_storage_container_update(StorageContainer $container) {
}


/**
 * Provide info about a container.
 *
 * @param $container
 *   Container.
 * @return
 *   An associative array of information items where the key is the translated name of the item.
 */
function hook_storage_container_info(StorageContainer $container) {
}


/**
 * Destroy a container.
 *
 * @param $container
 *   
 * @return
 *   
 */
function hook_storage_container_destroy(StorageContainer $container) {
}


/**
 * Create an instance in a container.
 *
 * @param $container
 *   Container that is to hold the file instance.
 * @param $file
 *   File to have an instance created.
 * @return
 *   Success boolean value.
 */
function hook_storage_instance_create(StorageContainer $container, $file) {
}


/**
 * Create an instance in a container by copying it from another.
 *
 * @param $container
 *   Container that is to hold the file instance.
 * @param $file
 *   File to have an instance created.
 * @param $source_container
 *   Container that the file instance should be copied from.
 * @return
 *   Success boolean value.
 */
function hook_storage_instance_copy(StorageContainer $container, $file, $source_container) {
}


/**
 * Destroy an instance.
 *
 * @param $container
 *   Container that holds the file instance.
 * @param $file
 *   File to have an instance destroyed.
 * @return
 *   Success boolean value.
 */
function hook_storage_instance_destroy(StorageContainer $container, $file) {
}


/**
 * Get a local filepath of an instance.
 *
 * @param $container
 *   Container that holds the file instance.
 * @param $file
 *   File to have local access.
 * @return
 *   Filepath or FALSE for failure.
 */
function hook_storage_instance_get_filepath(StorageContainer $container, $file) {
}


/**
 * Serve an instance.
 *
 * @param $container
 *   Container that holds the file instance.
 * @param $file
 *   File to be served.
 * @return
 *   An absolute URL.
 */
function hook_storage_instance_serve(StorageContainer $container, $file) {
}


/**
 * Returns information required to stream an instance with RTMP.
 *
 * @param $container
 *   Container that holds the instance.
 * @param $file
 *   File representing the object to be served.
 * @return
 *   An associative array of information to stream the file with RTMP:
 *     - 'streamer'
 *     - 'file'
 */
function hook_storage_instance_serve_rtmp(StorageContainer $container, $file) {
}

