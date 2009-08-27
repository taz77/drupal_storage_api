<?php
// $Id$


/**
 * @file
 *
 * Modules can access control to files via these hooks.
 *
 */

/**
 * @addtogroup hooks
 * @{
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

