<?php
// $Id$

// Copyright 2010 Jonathan Brown

class StorageService {

  public function __construct($service_id) {
    
    $this->service_id = $service_id;
    $this->info = storage_service_info($this->service_id);
  }
  
  public function name() {
    return check_plain($this->info['name']);
  }
  
  public function path() {
    return 'admin/structure/storage/services/' . $this->service_id;
  }
  
  public function link() {
    return l($this->info['name'], $this->path());
  }
  
  public function flushServingContainer() {

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


