<?php

// Copyright 2010-2011 Jonathan Brown

abstract class StorageContainer {

  public function __construct($fields, $unserialize = TRUE) {
  
    if ($unserialize && isset($container['settings'])) {
      $container['settings'] = unserialize($container['settings']);
    }
    
    foreach ($fields as $key => $value) {
      $this->$key = $value;
    }
  }
  
  public function service() {
    return storage_service_load($this->service_id);
  }

  public function count() {
  
    return db_select('storage_instance')
      ->condition('container_id', $this->container_id)
      ->isNotNull('file_id')
      ->countQuery()
      ->execute()
      ->fetchField();
  }

  public function size($format = FALSE) {

    $query = db_select('storage_instance');
    $query->join('storage_file', NULL, 'storage_instance.file_id = storage_file.file_id');
    $query->addExpression('SUM(storage_file.size)', 'size');

    $size = (int) $query->condition('container_id', $this->container_id)
      ->execute()
      ->fetchField();
      
    return $format ? storage_format_size($size) : $size;
  }
  
  private function htaccessCreate() {

    $lines = array(
      'SetHandler Drupal_Security_Do_Not_Remove_See_SA_2006_006',
      'Options None',
      'Options +FollowSymLinks',
    );
    
    if ($this->access_control) {
      $lines[] = 'Deny from all';
    }
    else {
      $lines[] = '<IfModule mod_headers.c>';
      $lines[] = '  Header set Content-Disposition "attachment"';
      $lines[] = '</IfModule>';
    }

    $temp_uri = storage_api_temp_uri();
    file_put_contents($temp_uri, implode("\n", $lines) . "\n");
    
    $storage = new Storage(array('filename' => '.htaccess'));
    $this->serviceInstanceCreate($storage, $temp_uri);
    unlink($temp_uri);
  }

  /**
   * Creates a storage container.
   *
   * @throws StorageException
   *   When it wasn't possible to create the container.
   */
  public function create() {
  
    try {
      $this->serviceCreate();
    }
    catch (StorageException $e) {
      $msg = 'Failed to create storage container !container.';
      watchdog('storage_api', $msg, array('!container' => $this->name()), WATCHDOG_ERROR);
      throw new StorageException(t($msg, array('!container' => $this->name())));
    }
    
    drupal_write_record('storage_container', $this);

    if ($this->servicePostCreate()) {
      $this->update();
    }
    
    if ($this->service()->info['htaccess']) {
      $this->htaccessCreate();
    }

    $msg = 'Storage container !container has been created.';
    drupal_set_message(t($msg, array('!container' => $this->link())));
    watchdog('storage_api', $msg, array('!container' => $this->name()), WATCHDOG_NOTICE, 
      l(t('view'), $this->path()));
  }

  public function name() {
    return check_plain($this->name);
  }
  
  public function path() {
    return 'admin/structure/storage/containers/' . $this->container_id;
  }
  
  public function link() {
    return l($this->name, $this->path());
  }
  
  public function update() {
    drupal_write_record('storage_container', $this, 'container_id');
  }

  public function flushServingContainer() {

    $selector_subquery = db_select('storage_class_container');
    $selector_subquery->join('storage_selector', NULL, 'storage_class_container.class_id = storage_selector.class_id');
      
    $selector_subquery->fields('storage_selector', array('selector_id'))
      ->condition('storage_class_container.container_id', $this->container_id);
      
    db_update('storage')
      ->expression('serving_container', 'NULL')
      ->condition('selector_id', $selector_subquery, 'IN')
      ->execute();

    cache_clear_all();
  }


  public function instanceCreate(Storage $storage, $uri) {
  
    $reference = $this->serviceInstanceCreate($storage, $uri);

    $instance = array(
      'container_id' => $this->container_id,
      'file_id' => $storage->file_id,
      'reference' => $reference,
    );

    drupal_write_record('storage_instance', $instance);
    
    $storage->flushServingContainer();
    
    $message = 'File instance created: ' . $storage->filename . '<br />';
    $message .= $this->logInfo();
    $message .= $storage->logInfo();

    watchdog('storage_api', $message, NULL);

    return $reference;
  }


  public function instanceCopy(Storage $storage, $source_container) {

    $reference = $this->serviceInstanceCopy($storage, $source_container);

    $instance = array(
      'container_id' => $this->container_id,
      'file_id' => $storage->file_id,
      'reference' => $reference,
    );

    drupal_write_record('storage_instance', $instance);
    
    $storage->flushServingContainer();
    
    $message = 'File instance copied: ' . $storage->filename . '<br />';
    $message .= $this->logInfo();
    $message .= $storage->logInfo();

    watchdog('storage_api', $message, NULL);
  }


  public function tryInstanceCopy(Storage $storage) {

    // Find instances of the storage's file in other containers of the same type
    // that are in the storage's class.
    $query = db_select('storage_instance', NULL, array('fetch' => PDO::FETCH_ASSOC));
    $query->join('storage_container', NULL, 'storage_instance.container_id = storage_container.container_id');
    $query->join('storage_class_container', NULL, 'storage_instance.container_id = storage_class_container.container_id');
    $query->join('storage_selector', NULL, 'storage_class_container.class_id = storage_selector.class_id');

    $result = $query->fields('storage_instance', array('container_id', 'reference'))
      ->condition('storage_instance.file_id', $storage->file_id)
      ->condition('storage_instance.container_id', $this->container_id, '!=')
      ->condition('storage_container.service_id', $this->service_id)
      ->condition('storage_selector.selector_id', $storage->selector_id)
      ->orderBy('storage_selector.migrating')
      ->orderBy('storage_class_container.weight', 'DESC')
      ->orderBy('storage_class_container.container_id')
      ->execute();

    foreach ($result as $row) {
      $source_container = storage_container_load($row['container_id']);
      $source_container->reference = $row['reference'];

      try {
        $this->instanceCopy($storage, $source_container);
      }
      catch (StorageException $e) {
        continue;
      }

      return TRUE;
    }

    return FALSE;
  }

  /**
   * Ensure that an instance exists in the container.
   *
   * @param $storage
   *   The storage that there must be an instance of.
   *
   * @throws StorageException
   *   When it isn't possible to ensure that the instance exists.
   */
  public function ensureInstanceExists(Storage $storage) {
    $check = FALSE;

    if ($this->suspended != 0) {
      throw new StorageException();
    }
    
    if ($storage->file_id) {
      if ($this->instanceExists($storage)) {
        return;
      }
    }
    else {
      // We don't have a file_id yet, so we might check for an instance once we do.
      $check = TRUE;
    }
    
    // Is it possible to short circuit?
    if ($this->service()->info['can_copy'] && !$check) {
      if ($this->tryInstanceCopy($storage)) {
        return;
      }
    }

    $uri = $storage->getUri();
    $exists = FALSE;

    // If it's a new file, there is no point in checking for an instance.
    if ($check && !$storage->new_file) {
      $exists = $this->instanceExists($storage);
    }
    
    if (!$exists) {
      try {
        $this->instanceCreate($storage, $uri);
      }
      catch (StorageException $e) {}
    }
    
    // Delete the intermediate file if it is temporary.
    storage_api_temp_uri_dispose($uri);
    
    if (isset($e)) {
      throw $e;
    }
  }


  public function instanceExists(Storage $storage) {

    return db_select('storage_instance')
      ->fields('storage_instance', array('reference'))
      ->condition('file_id', $storage->file_id)
      ->condition('container_id', $this->container_id)
      ->execute()
      ->fetchField();
  }


  /**
   * Destroy an instance if it isn't required by any other storages.
   *
   * @param $storage
   *   The storage to have its instance destroyed.
   * @param $reference
   *   The reference of the instance to be destroyed.
   *
   * @throws StorageException
   *   When the instance should be destroyed, but it isn't possible.
   */
  public function instanceDestroyIfUnrequired(Storage $storage, $reference) {

    // Find other storages that use the same file.
    $storage_ids = db_select('storage')
      ->fields('storage', array('storage_id'))
      ->condition('storage.file_id', $storage->file_id)
      ->condition('storage.storage_id', $storage->storage_id, '!=')
      ->execute()
      ->fetchCol();

    if (count($storage_ids)) {

      // Are any of the other storages supposed to have an instance in this
      // container?
      $query = db_select('storage');
      $query->join('storage_selector', NULL, 'storage.selector_id = storage_selector.selector_id');
      $query->join('storage_class_container', NULL, 'storage_selector.class_id = storage_class_container.class_id');

      $count = $query->condition('storage.storage_id', $storage_ids, 'IN')
        ->condition('storage_class_container.container_id', $this->container_id)
        ->countQuery()
        ->execute()
        ->fetchField();

      if ($count > 0) {
        return;
      }

      // Do any of the other storages have this container as their initial
      // container?
      $count = db_select('storage')
        ->condition('storage_id', $storage_ids, 'IN')
        ->condition('initial_container_id', $this->container_id)
        ->countQuery()
        ->execute()
        ->fetchField();

      if ($count > 0) {
        return;
      }
    }

    $this->instanceDestroy($storage, $reference);
  }


  /**
   * Destroy an instance in the container
   *
   * @param $storage
   *   The storage to have its instance destroyed.
   * @param $reference
   *   The reference of the instance to be destroyed.
   *
   * @throws StorageException
   *   When it isn't possible to destroy the instance.
   */
  public function instanceDestroy(Storage $storage, $reference) {

    if ($this->suspended != 0) {
      throw new StorageException();
    }

    $this->serviceInstanceDestroy($reference);

    db_delete('storage_instance')
      ->condition('file_id', $storage->file_id)
      ->condition('container_id', $this->container_id)
      ->execute();
    
    $message = 'File instance destroyed: ' . $storage->filename . '<br />';
    $message .= $this->logInfo();
    $message .= $storage->logInfo();

    watchdog('storage_api', $message, NULL);
  }
  
  
  public function instanceServeURL($reference) {
  
    if (!$this->service()->info['direct']) {    // direct services don't return a URL
      return $this->serviceInstanceServe($reference);
    }
  }


  public function instanceLink($reference) {
  
    $url = $this->instanceServeURL($reference);
    
    if ($url !== '') {
      return l($reference, $url);
    }
    else {
      return $reference;
    }
  }


  public function classes() {
    $classes = array();

    $result = db_select('storage_class_container', NULL, array('fetch' => PDO::FETCH_ASSOC))
      ->fields('storage_class_container', array('class_id'))
      ->condition('container_id', $this->container_id)
      ->orderBy('class_id')
      ->execute();

    foreach ($result as $class_info) {
      $classes[] = storage_class_load($class_info['class_id']);
    }

    return $classes;
  }
  

  /**
   * Destroy container.
   */
  public function destroy($just_delete = FALSE) {

    $this->flushServingContainer();

    db_delete('storage_container')
      ->condition('container_id', $this->container_id)
      ->execute();

    db_delete('storage_class_container')
      ->condition('container_id', $this->container_id)
      ->execute();

    db_delete('storage_instance')
      ->condition('container_id', $this->container_id)
      ->execute();

    if (!$just_delete) {
      $this->serviceDestroy();
    }

    $message = 'Storage container <i>' . $this->name() . '</i> has been destroyed.';
    drupal_set_message($message);
    watchdog('storage_api', $message, NULL);
  }

  /**
   * Determines if a reference exists in a container.
   *
   * The reference may either have a record in the database, or exist in the
   * container itself.
   *
   * @param $reference
   *   The reference to look for.
   * @return
   *   Whether the reference exists in the container.
   */
  public function referenceExists($reference) {

    $exists = (bool) db_select('storage_instance')
      ->condition('container_id', $this->container_id)
      ->condition('reference', $reference)
      ->countQuery()
      ->execute()
      ->fetchField();
      
    if ($exists) {
      return TRUE;
    }
    
    return $this->serviceInstanceExists($reference);
  }

  /**
   * Generates a unique filename for a container, based on a desired filename.
   *
   * @param $filename
   *   Desired filename.
   * @return
   *   Unique filename.
   */
  public function uniqueFilename($filename) {

    if ($this->referenceExists($filename)) {

      $pos = strrpos($filename, '.');
      
      if ($pos !== FALSE) {
        $name = substr($filename, 0, $pos);
        $ext = substr($filename, $pos);
      }
      else {
        $name = $filename;
        $ext = '';
      }

      $count = 0;
      
      do {
        $filename = $name . '_' . $count++ . $ext;
        
      } while ($this->referenceExists($filename));
    }

    return $filename;
  }

  public function logInfo() {

    $info = 'container: ' . $this->name() . ', <br />';

    return $info;
  }

  public function serviceManifest(&$sandbox, &$finished) {
  
    if (!isset($sandbox['references'])) {
      $sandbox['references'] = $this->serviceListReferences();
    }

    $count = 100;
    $manifest = array();
    
    while ($count-- && ($reference = array_shift($sandbox['references']))) {
    
      try {
        $info = $this->serviceInstanceCheck($reference);
      }
      catch (StorageException $e) {
        continue;
      }

      $info['reference'] = $reference;
      $manifest[] = $info;
    }
    
    $finished = (count($sandbox['references']) == 0);

    return $manifest;
  }

  /**
   * Get information about an instance.
   *
   * @param $reference
   *   The reference of the instance the be examined.
   *
   * @throws StorageException
   *   When it isn't possible to get information about the instance.
   */
  public function serviceInstanceCheck($reference) {
    $uri = $this->serviceInstanceGet($reference);
    $stat = @stat($uri);
    
    // Delete the file if it is temporary.
    storage_api_temp_uri_dispose($uri);

    if ($stat === FALSE) {
      throw new StorageException();
    }

    $info = array(
      'size' => $stat['size'],
      'md5' => hash_file('md5', $uri, TRUE),
    );

    return $info;
  }

  public function serviceInstanceServe($reference) {

    return file_create_url($this->settings['base_url'] . '/' . $reference);
  }

  // Optional API functions.
  public function serviceCheck() {}
  public function servicePostCreate() {}
  public function serviceUpdate() {}
  public function serviceMaintenance() {}
  public function serviceInstanceCopy(Storage $storage, StorageContainer $source_container) {}
}


interface StorageServiceInterface {

  public function serviceCheck();
  public function serviceSettingsForm($edit = FALSE);
  public function serviceSettingsValidate($edit = FALSE);
  public function serviceCreate();
  public function servicePostCreate();
  public function serviceInfo();
  public function serviceUpdate();
  public function serviceDestroy();
  public function serviceMaintenance();
  public function serviceInstanceCreate(Storage $storage, $uri);
  public function serviceInstanceCopy(Storage $storage, StorageContainer $source_container);
  public function serviceInstanceExists($reference);
  public function serviceInstanceCheck($reference);
  public function serviceInstanceGet($reference);
  public function serviceInstanceDestroy($reference);
  public function serviceInstanceServe($reference);
}

