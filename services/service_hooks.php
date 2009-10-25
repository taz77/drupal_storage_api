<?php
// $Id$


/**
 * @file
 * Storage service modules expose themselves to Storage API via these hooks.
 *
 * Storage service modules must always be prefixed with 'storage_'.
 */

/**
 * @addtogroup hooks
 * @{
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
 * @return
 *   
 */
function hook_storage_container_settings_form($settings, $serving) {
}


/**
 * Validate the settings for a storage container.
 *
 * @param $container
 *   
 * @return
 *   
 */
function hook_storage_container_validate($container) {
}


/**
 * Create a storage container.
 *
 * @param $container
 *   Container to be created.
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

