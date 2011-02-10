<?php
// $Id$

// Copyright 2010 Jonathan Brown

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

    $size = $query->condition('container_id', $this->container_id)
      ->execute()
      ->fetchField();
      
    return $format ? storage_format_size($size) : $size;
  }

  /**
   * Creates a storage container.
   */
  public function create() {
  
    try {
      $this->serviceCreate();
    }
    catch (Exception $e) {
      $msg = 'Failed to create storage container !container.';
      watchdog('storage_api', $msg, array('!container' => $this->name()), WATCHDOG_ERROR);
      throw new Exception(t($msg, array('!container' => $this->name())));
    }
    
    drupal_write_record('storage_container', $this);
    $this->servicePostCreate();

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


  public function instanceCreate(Storage $storage, array $source) {
  
    // We need write access to the directory for it to be deleted.
    if (empty($source['keep_me'])) {
    
      if (!is_writable(dirname($source['filepath']))) {
        $source['keep_me'] = TRUE;
      }
    }
    
    $reference = $this->serviceInstanceCreate($storage, $source);

    if (!$source['keep_me']) {
      @unlink($source['filepath']);
    }

    $instance = array(
      'container_id' => $this->container_id,
      'file_id' => $storage->file_id,
    );

    if ($reference) {
      $instance['reference'] = $reference;
    }

    drupal_write_record('storage_instance', $instance);
    
    $storage->flushServingContainer();
    
    $message = 'File instance created' . ($storage->filename ? (': ' . $storage->filename . ' ') : '') . '. <br />';
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
    );

    if ($result !== TRUE) {
      $instance['reference'] = $reference;
    }

    drupal_write_record('storage_instance', $instance);
    
    $storage->flushServingContainer();
    
    $message = 'File instance copied' . ($storage->filename ? (': ' . $storage->filename . ' ') : '') . '. <br />';
    $message .= $this->logInfo();
    $message .= $storage->logInfo();

    watchdog('storage_api', $message, NULL);
  }


  public function tryInstanceCopy(Storage $storage) {
    
    $candiate_container_ids = $storage->copyCandidateContainers($this);
    
    foreach ($candiate_container_ids as $candiate_container_id) {

      if ($container->instanceExists($storage)) {
        $source_container = storage_container_load($candiate_container_id);
        return _storage_instance_copy($container, $storage, $source_container);
      }
    }
  }

  public function ensureInstanceExists(Storage $storage) {
    $check = FALSE;
    $keep_me = TRUE;

    if ($this->suspended != 0) {
      return FALSE;
    }
    
    if ($storage->file_id) {
      if ($this->instanceExists($storage)) {
        return TRUE;
      }
    }
    else {
      // We don't have a file_id yet, so we might check for an instance once we do.
      $check = TRUE;
    }
    
    // Is it possible to short circuit?
    if ($this->service()->info['can_copy'] && !$check) {
      if ($this->tryInstanceCopy($storage)) {
        return TRUE;
      }
    }

    $filepath = $storage->getLocalFilepath($keep_me);
    
    $exists = FALSE;
    
    if ($filepath) {
    
      // If it's a new file, there is no point in checking for an instance.
      if ($check && !$storage->new_file) {
        $exists = $this->instanceExists($storage);
      }
      
      if (!$exists) {

        $source = array(
          'filepath' => $filepath,
          'keep_me' => $keep_me,
        );

        $this->instanceCreate($storage, $source);
        $exists = TRUE;
      }
      
      if (!$keep_me) {
        @unlink($filepath);
      }
    }
    
    return $exists;
  }


  public function instanceExists(Storage $storage) {

    return db_select('storage_instance')
      ->fields('storage_instance', array('reference'))
      ->condition('file_id', $storage->file_id)
      ->condition('container_id', $this->container_id)
      ->execute()
      ->fetchField();
  }


  public function instanceDestroyIfUnrequired(Storage $storage, $reference) {

    // Are there any other instances in this container that use the same file?
    
    // We don't destroy file instances in a container in a class that a file is migrating away from
    // because that instance might not have been migrated yet 
    // - so there is no {storage_selector}.migrating = 0 condition

    $query = db_select('storage');
    $query->join('storage_selector', NULL, 'storage.selector_id = storage_selector.selector_id');
    $query->join('storage_class_container', NULL, 'storage_selector.class_id = storage_class_container.class_id');

    $count = $query->condition('storage.storage_id', $storage->storage_id, '!=')
      ->condition('storage.file_id', $storage->file_id)
      ->condition('storage_class_container.container_id', $this->container_id)
      ->countQuery()
      ->execute()
      ->fetchField();
      
    $failure = FALSE;
    
    if ($count == 0) {
      $success = $this->instanceDestroy($storage, $reference);
      
      if (!$success)
        $failure = TRUE;
    }
    
    return !$failure;
  }


  public function instanceDestroy(Storage $storage, $reference) {

    if ($this->suspended != 0) {
      return FALSE;
    }

    $this->serviceInstanceDestroy($reference);

    db_delete('storage_instance')
      ->condition('file_id', $storage->file_id)
      ->condition('container_id', $this->container_id)
      ->execute();
    
    $message = 'File instance destroyed' . ($storage->filename ? (': ' . $storage->filename . ' ') : '') . '. <br />';
    $message .= $this->logInfo();
    $message .= $storage->logInfo();

    watchdog('storage_api', $message, NULL);

    return TRUE;
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


  /**
   * Remove a container from all classes and schedule instances to be destroyed.
   */
  public function drain() {
    $selector_subquery = db_select('storage_class_container');
    $selector_subquery->join('storage_selector', NULL, 'storage_class_container.class_id = storage_selector.class_id');
    
    $selector_subquery->fields('storage_selector', array('selector_id'))
      ->condition('storage_class_container.container_id', $this->container_id);
      
    db_update('storage')
      ->fields(array('check_me' => 1))
      ->expression('serving_container', 'NULL')
      ->condition('selector_id', $selector_subquery, 'IN')
      ->execute();

    db_delete('storage_class_container')
      ->condition('container_id', $this->container_id)
      ->execute();

    $message = 'Storage container <i>' . $this->name() . 
      '</i> has been removed from all classes and will be drained during cron.';

    drupal_set_message($message);
    watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $this->path()));
  }


  /**
   * Destroy container.
   */
  public function destroy() {
  
    db_delete('storage_container')
      ->condition('container_id', $this->container_id)
      ->execute();

    db_delete('storage_class_container')
      ->condition('container_id', $this->container_id)
      ->execute();

    db_delete('storage_instance')
      ->condition('container_id', $this->container_id)
      ->execute();
      
    $this->flushServingContainer();
    $this->serviceDestroy();

    $message = 'Storage container <i>' . $this->name() . '</i> has been destroyed.';
    drupal_set_message($message);
    watchdog('storage_api', $message, NULL);
  }

  /**
   * Determines if a file's reference already exists in a container.
   *
   * @param $reference
   *   The reference to look for.
   * @return
   *   Whether the reference exists in the container.
   */
  public function referenceExists($reference) {

    return (bool) db_select('storage_instance')
      ->condition('container_id', $this->container_id)
      ->condition('reference', $reference)
      ->countQuery()
      ->execute()
      ->fetchField();
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
      }

      $counter = 0;
      
      do {
        $filename = $name . '_' . $counter++ . $ext;
        
      } while($this->referenceExists($filename));
    }

    return $filename;
  }

  public function logInfo() {

    $info = 'container: ' . $this->name() . ', <br />';

    return $info;
  }

  // Optional API functions.
  public function serviceCheck() {
    return TRUE;
  }
  
  public function servicePostCreate() {}
  public function serviceUpdate() {}
  public function serviceMaintenance() {}
  public function serviceInstanceCopy(Storage $storage, $reference, $source_container) {}
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
  public function serviceInstanceCreate(Storage $storage, array $source);
  public function serviceInstanceCopy(Storage $storage, $reference, $source_container);
  public function serviceInstanceCheck($reference);
  public function serviceInstanceDestroy($reference);
  public function serviceInstanceGetFilepath($reference);
  public function serviceInstanceServe($reference);
}

