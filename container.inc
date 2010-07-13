<?php
// $Id$

// Copyright 2010 Jonathan Brown

class StorageContainer {

  public function __construct($container, $unserialize = TRUE) {
  
    if (!is_array($container)) {
    
      $container = db_select('storage_container')
        ->fields('storage_container')
        ->condition('container_id', $container)
        ->execute()
        ->fetchAssoc();

      if (!$container)
        throw new Exception();
    }
    
    if ($unserialize) {
      $container['settings'] = unserialize($container['settings']);
    }

    foreach ($container as $key => $value)
      $this->$key = $value;
  }
  
  public function service() {
    return new StorageService($this->service_id);
  }
        
  public function settings_form($edit = FALSE) {
    return _storage_service_invoke($this->service_id, 'container_settings_form', $this, $edit);
  }

  public function validate($edit = FALSE) {
    _storage_service_invoke($this->service_id, 'container_validate', $this, $edit);
  }
  
  /**
   * Creates a storage container.
   */
  public function create() {

    try {
      _storage_service_invoke($this->service_id, 'container_create', $this);
    }
    catch (Exception $e) {
      $msg = 'Failed to create storage container !container.';
      drupal_set_message(t($msg, array('!container' => $this->name())), 'error');
      watchdog('storage_api', $msg, array('!container' => $this->name()), WATCHDOG_ERROR);
      throw new Exception();
    }
    
    drupal_write_record('storage_container', $this);
    _storage_service_invoke($this->service_id, 'container_postcreate', $this);

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

  public function flush_serving_container() {

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


  public function instance_create(Storage $storage, $source) {
  
    // We need write access to the directory for it to be deleted.
    if (empty($source['keep_me'])) {
    
      if (!is_writable(dirname($source['filepath']))) {
        $source['keep_me'] = TRUE;
      }
    }

    $result = _storage_service_invoke($this->service_id, 'instance_create', $this, $storage, $source);
    
    if (!$result) {
      throw new Exception();
    }
    
    if (!$source['keep_me']) {
      @unlink($source['filepath']);
    }
      
    $instance = array(
      'container_id' => $this->container_id,
      'file_id' => $storage->file_id,
    );

    if ($result !== TRUE) {
      $instance['reference'] = $result;
    }

    drupal_write_record('storage_instance', $instance);
    
    $storage->flush_serving_container();
    
    $message = 'File instance created' . ($storage->filename ? (': ' . $storage->filename . ' ') : '') . '. <br />';
    $message .= $this->log_info();
    $message .= $storage->log_info();

    watchdog('storage_api', $message, NULL);

    return $result;
  }


  public function instance_copy(Storage $storage, $source_container) {

    $result = _storage_service_invoke($this->service_id, 'instance_copy', $this, $storage, $source_container);
    
    if (!$result) {
      throw new Exception();
    }
      
    $instance = array(
      'container_id' => $this->container_id,
      'file_id' => $storage->file_id,
    );

    if ($result !== TRUE) {
      $instance['reference'] = $result;
    }

    drupal_write_record('storage_instance', $instance);
    
    $storage->flush_serving_container();
    
    $message = 'File instance copied' . ($storage->filename ? (': ' . $storage->filename . ' ') : '') . '. <br />';
    $message .= $this->log_info();
    $message .= $storage->log_info();

    watchdog('storage_api', $message, NULL);

    return $result;
  }


  public function try_instance_copy(Storage $storage) {
    
    $candiate_container_ids = $storage->copy_candidate_containers($this);
    
    foreach ($candiate_container_ids as $candiate_container_id) {

      if ($container->instance_exists($storage)) {
        $source_container = storage_container_load($candiate_container_id);
        return _storage_instance_copy($container, $storage, $source_container);
      }
    }
  }


  public function ensure_instance_exists(Storage $storage) {
    $check = FALSE;
    $keep_me = TRUE;

    if ($this->suspended != 0 ||
      $this->service_suspended != 0)
    {
      return FALSE;
    }
    
    if ($storage->file_id) {
      if ($this->instance_exists($storage)) {
        return TRUE;
      }
    }
    else {
      // We don't have an file_id yet, so we might check for an instance once we do.
      $check = TRUE;
    }
    
    // Is it possible to short circuit?
    $service_info = $this->service()->info();

    if ($service_info['can_copy'] && !$check) {
      if ($this->try_instance_copy($storage)) {
        return TRUE;
      }
    }

    $filepath = $storage->get_local_filepath($keep_me);
    
    $exists = FALSE;
    
    if ($filepath) {
    
      // If it's a new file, there is no point in checking for an instance.
      if ($check && !$storage->new_file) {
        $exists = $this->instance_exists($storage);
      }
      
      if (!$exists) {

        $source = array(
          'filepath' => $filepath,
          'keep_me' => $keep_me,
        );

        $exists = $this->instance_create($storage, $source);
      }
      
      if (!$keep_me) {
        @unlink($filepath);
      }
    }
    
    return $exists;
  }


  public function instance_exists(Storage $storage) {

    return db_select('storage_instance')
      ->fields('storage_instance', array('reference'))
      ->condition('file_id', $storage->file_id)
      ->condition('container_id', $this->container_id)
      ->execute()
      ->fetchField();
  }


  public function instance_destroy_if_unrequired(Storage $storage, $reference) {

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
      $success = $this->instance_destroy($storage, $reference);
      
      if (!$success)
        $failure = TRUE;
    }
    
    return !$failure;
  }


  public function instance_destroy(Storage $storage, $reference) {

    if ($this->suspended != 0 ||
      $this->service_suspended != 0)
    {
      return FALSE;
    }

    $success = _storage_service_invoke($this->service_id, 'instance_destroy', $this, $storage, $reference);
      
    if (!$success)
      return FALSE;

    db_delete('storage_instance')
      ->condition('file_id', $storage->file_id)
      ->condition('container_id', $this->container_id)
      ->execute();
    
    $message = 'File instance destroyed' . ($storage->filename ? (': ' . $storage->filename . ' ') : '') . '. <br />';
    $message .= $this->log_info();
    $message .= $storage->log_info();

    watchdog('storage_api', $message, NULL);

    return TRUE;
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
      
    $this->flush_serving_container();

    $success = _storage_service_invoke($this->service_id, 'container_destroy', $this);

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
  public function reference_exists($reference) {

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
  public function unique_filename($filename) {

    if ($this->reference_exists($filename)) {

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
        
      } while($this->reference_exists($filename));
    }

    return $filename;
  }

  public function log_info() {

    $info = 'container: ' . $this->name() . ', <br />';

    return $info;
  }
}

