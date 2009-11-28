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
 * @param $file
 *   The file to be generated.
 * @return
 *   Filepath to the generated file. It will be deleted.
 */
function hook_storage_file_generate($file) {
}


/**
 * Access hooks
 */


/**
 * Determine the current user's access to a file.
 *
 * @param $type
 *   Module-specific file type identifier.
 * @param $file_id
 *   file_id of the file.
 * @return
 *   Whether access is permitted or not.
 *   - TRUE for permitted.
 *   - FALSE for denied.
 *   - NULL for check node_access() .
 */
function hook_storage_access($type, $file_id) {
}


/**
 * Alter the current user's access to a file that belongs to another module.
 *
 * @param $module
 *  Module that owns the file.
 * @param $type
 *  Module-specific file type identifier.
 * @param $file_id
 *  file_id of the file.
 * @return
 *   Access alteration.
 *   - TRUE for permitted.
 *   - FALSE for denied.
 *   - NULL for don't alter.
 */
function hook_storage_access_alter($module, $type, $file_id) {
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
 *     - 'dynamic'
 *       Whether the service can serve an object instance with a dynamically set filename.
 *       If TRUE, hook_storage_file_*() should not be implemented.
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
 * @param $container
 *   The container to be validated.
 * @param $edit
 *   Whether this is a container being edited.
 */
function hook_storage_container_validate($container, $edit) {
}


/**
 * Create a storage container.
 *
 * @param $container
 *   Container to be created.
 * @return
 *   The container (with any modifications).
 */
function hook_storage_container_create($container) {
}


/**
 * Provide info about a container.
 *
 * @param $container
 *   Container.
 * @return
 *   An associative array of information items where the key is the translated name of the item.
 */
function hook_storage_container_info($container) {
}


/**
 * Destroy a container.
 *
 * @param $container
 *   
 * @return
 *   
 */
function hook_storage_container_destroy($container) {
}


/**
 * Create an object instance in a container.
 *
 * @param $container
 *   Container that is to hold the object instance.
 * @param $file
 *   File representing the object to have an instance created.
 * @return
 *   Success boolean value.
 */
function hook_storage_object_instance_create($container, $file) {
}


/**
 * Create an object instance in a container by copying it from another.
 *
 * @param $container
 *   Container that is to hold the object instance.
 * @param $file
 *   File representing the object to have an instance created.
 * @param $source_container
 *   Container that the object instance should be copied from.
 * @return
 *   Success boolean value.
 */
function hook_storage_object_instance_copy($container, $file, $source_container) {
}


/**
 * Destroy an object instance.
 *
 * @param $container
 *   Container that holds the object instance.
 * @param $file
 *   File representing the object to have an instance destroyed.
 * @return
 *   Success boolean value.
 */
function hook_storage_object_instance_destroy($container, $file) {
}


/**
 * Get a local filepath of an object instance.
 *
 * @param $container
 *   Container that holds the object instance.
 * @param $file
 *   File representing the object to have local access.
 * @return
 *   Filepath or FALSE for failure.
 */
function hook_storage_object_instance_get_filepath($container, $file) {
}


/**
 * Serve an object instance.
 *
 * If serving directly, output the HTTP headers, followed by the file contents and then exit() .
 *
 * Otherwise, just return a URL.
 *
 * @param $container
 *   Container that holds the object instance.
 * @param $file
 *   File representing the object to be served.
 * @return
 *   An absolute URL.
 */
function hook_storage_object_instance_serve($container, $file) {
}


/**
 * Create a file instance in a container.
 *
 * Don't implement for dynamic services.
 *
 * @param $container
 *   Container that is to hold the file instance.
 * @param $file
 *   File to have an instance created.
 * @return
 *   Success boolean value.
 */
function hook_storage_file_instance_create($container, $file) {
}


/**
 * Create a file instance in a container by copying it from another.
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
function hook_storage_file_instance_copy($container, $file, $source_container) {
}


/**
 * Destroy a file instance.
 *
 * Don't implement for dynamic services.
 *
 * @param $container
 *   Container that holds the file instance.
 * @param $file
 *   File to have an instance destroyed.
 * @return
 *   Success boolean value.
 */
function hook_storage_file_instance_destroy($container, $file) {
}


/**
 * Get a local filepath of a file instance.
 *
 * Don't implement for dynamic services.
 *
 * @param $container
 *   Container that holds the file instance.
 * @param $file
 *   File to have local access.
 * @return
 *   Filepath or FALSE for failure.
 */
function hook_storage_file_instance_get_filepath($container, $file) {
}


/**
 * Serve a file instance.
 *
 * Don't implement for dynamic services.
 *
 * @param $container
 *   Container that holds the file instance.
 * @param $file
 *   File to be served.
 * @return
 *   An absolute URL.
 */
function hook_storage_file_instance_serve($container, $file) {
}

