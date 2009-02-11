<?php
// $Id$

// Copyright 2009 Jonathan Brown


function storage_api_views_tables2($table_name, $field_name, $prefix, $name) {

  $tables[$prefix . '_storage_files'] = array(
    'name' => 'storage_files',
    'join' => array(
      'left' => array(
        'table' => $table_name,
        'field' => $field_name
      ),
      'right' => array(
        'field' => 'file_id'
      )
    ),
    'fields' => array(
      'servings' => array(
        'name' => $name . t(': Servings'),
        'help' => t("Display the number of times the file has been served."),
        'sortable' => TRUE
      ),
    ),
    'sorts' => array(
      'servings' => array(
        'name' => $name . t(': Servings'),
        'help' => t("Sort by the number of times the file has been served."),
      ),
    )
  );

  $tables[$prefix . '_storage_objects'] = array(
    'name' => 'storage_objects',
    'join' => array(
      'left' => array(
        'table' => $prefix . '_storage_files',
        'field' => 'object_id'
      ),
      'right' => array(
        'field' => 'object_id'
      )
    ),
    'fields' => array(
      'size' => array(
        'name' => $name . t(': Filesize'),
        'help' => t("Display the size of the file."),
        'sortable' => TRUE,
        'handler' => '_op_video_views_handler_bytesize'
      ),
    ),
    'sorts' => array(
      'size' => array(
        'name' => $name . t(': Filesize'),
        'help' => t("Sort by the size file."),
      ),
    )
  );
  
  return $tables;
}

