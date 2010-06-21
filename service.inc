<?php
// $Id$

// Copyright 2010 Jonathan Brown

class StorageService {

  function __construct($service) {
  
    if (!is_array($service)) {
    
      $service = db_select('storage_service')
        ->fields('storage_service')
        ->condition('service_id', $service)
        ->execute()
        ->fetchAssoc();
        
      if (!$service)
        throw new Exception();
    }
    
    $service += _storage_service_invoke($service['service_id'], 'service_info');

    foreach ($service as $key => $value)
      $this->$key = $value;
  }
  
  public function info() {
    return _storage_service_invoke($this->service_id, 'service_info');
  }

  function name() {
    return check_plain($this->name);
  }
  
  function path() {
    return 'admin/structure/storage/services/' . $this->service_id;
  }
  
  function link() {
    return l($this->name, $this->path());
  }
  
  function update() {
    drupal_write_record('storage_service', $this, 'service_id');
  }

  function flush_serving_container() {

    $selector_subquery = db_select('storage_container');
    $selector_subquery->join('storage_class_container', NULL, 'storage_container.container_id = storage_class_container.container_id');
    $selector_subquery->join('storage_selector', NULL, 'storage_class_container.class_id = storage_selector.class_id');
    
    $selector_subquery->fields('storage_selector', array('selector_id'))
      ->condition('storage_container.service_id', $this->service_id);
      
    db_update('storage')
      ->expression('serving_container', 'NULL')
      ->condition('selector_id', $selector_subquery, 'IN')
      ->execute();

    cache_clear_all();
  }
}

