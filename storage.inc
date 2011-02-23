<?php
// $Id$

// Copyright 2010-2011 Jonathan Brown

class Storage {

  public function __construct($storage) {
  
    if (!is_array($storage)) {
    
      // Either the file is being deleted and no longer has a selector,
      // or we grab the selector with migrating = 0.
      $query = db_select('storage');
      $query->leftJoin('storage_file', NULL, 'storage.file_id = storage_file.file_id');
      $query->leftJoin('storage_selector', NULL, 'storage.selector_id = storage_selector.selector_id');
      $query->leftJoin('storage_class', NULL, 'storage_selector.class_id = storage_class.class_id');
      
      $storage = $query->fields('storage')
        ->fields('storage_file')
        ->fields('storage_selector')
        ->fields('storage_class')
        ->condition('storage.storage_id', $storage)
        ->condition(db_or()
          ->isNull('storage.selector_id')
          ->condition('storage_selector.migrating', 0))
        ->execute()
        ->fetchAssoc();

      if (!$storage) {
        throw new Exception();
      }
    }
    
    $storage['data'] = unserialize($storage['data']);
    $storage['options'] = unserialize($storage['options']);

    if ($storage['serving_container']) {
      $storage['serving_container'] = unserialize($storage['serving_container']);
    }

    foreach ($storage as $key => $value) {
      $this->$key = $value;
    }
  }

  public function update() {
    drupal_write_record('storage', $this, 'storage_id');
  }
  
  public function logServing($container_id = NULL) {
    global $user;
    
    $serving = array(
      'storage_id' => $this->storage_id,
      'size' => $this->size,
      'uid' => $user->uid,
      'sid' => session_id(),
      'ip_addr' => ip2long(ip_address()),
      'timestamp' => REQUEST_TIME,
      'referrer' => substr($_SERVER['HTTP_REFERER'], 0, 255)
    );
    
    if ($container_id) {
      $serving['container_id'] = $container_id;
    }

    drupal_write_record('storage_serving', $serving);
    
    db_update('storage')
      ->fields(array('last_touched' => REQUEST_TIME))
      ->expression('servings', 'servings + :one', array(':one' => 1))   // apparently literals can't be in fragments
      ->condition('storage_id', $this->storage_id)
      ->execute();
  }
  
  public function touch() {

    $this->last_touched = REQUEST_TIME;

    db_update('storage')
      ->fields(array('last_touched' => $this->last_touched))
      ->condition('storage_id', $this->storage_id)
      ->execute();
  }

  /**
   * Determine whether the user has access to a file.
   *
   * @return
   *   Boolean TRUE if the current user has access to the file.
   */
  public function access() {

    if (empty($this->options['access_control'])) {
      return TRUE;
    }

    $results = module_invoke_all('storage_access_alter', $this->module, $this->type, $this->storage_id);
    
    foreach ($results as $result) {
      if (isset($result)) {
        return $result;
      }
    }
    
    if ($this->module) {
      $result = module_invoke($this->module, 'storage_access', $this->type, $this->storage_id);
      
      if (isset($result)) {
        return $result;
      }
    }
    
    if ($this->nid) {
      return node_access('view', node_load($this->nid));
    }
    
    return TRUE;
  }


  public function initialContainerInstance() {

    $container = storage_container_load($this->data['initial_container_id']);

    $container->reference = db_select('storage_instance')
      ->fields('storage_instance', array('reference'))
      ->condition('storage_instance.container_id', $container->container_id)
      ->condition('storage_instance.file_id', $this->file_id)
      ->range(0, 1)
      ->execute()
      ->fetchField();

    if (!$container->reference) {
      return FALSE;
    }

    return $container;
  }


  public function servingContainer() {

    $query = db_select('storage_instance');
    $query->join('storage_class_container', NULL, 'storage_instance.container_id = storage_class_container.container_id');
    $query->join('storage_selector', NULL, 'storage_class_container.class_id = storage_selector.class_id');
    $query->join('storage_container', NULL, 'storage_instance.container_id = storage_container.container_id');
    
    $container = $query->condition('storage_instance.file_id', $this->file_id)
      ->condition('storage_class_container.serving', STORAGE_API_SERVING, '>=')
      ->condition('storage_selector.selector_id', $this->selector_id)
      ->condition('storage_container.suspended', 0)
      ->fields('storage_container')
      ->fields('storage_instance', array('reference'))
      ->orderBy('migrating')
      ->orderBy('weight')
      ->orderBy('container_id')
      ->range(0, 1)
      ->execute()
      ->fetchAssoc();
      
    if ($container) {
      return storage_container_new($container);
    }
    
    // We couldn't serve the file from any of the containers it is supposed to be in
    // lets see if the file is in the storage's initial container.
    return $this->initialContainerInstance();
  }


  private function generate() {

    $container = storage_container_load($this->data['initial_container_id']);
    
    if (!$container)
      return FALSE;

    $filepath = $this->getLocalFilepath($keep_me);
    
    if (!$filepath)
      return FALSE;
      
    $source['filepath'] = $filepath;
    $source['keep_me'] = $keep_me;
    
    $link = $this->nid ? l(t('view'), 'node/' . $this->nid) : NULL;

    // If the storage's file is already in the system, then the instance might already exist.
    if (empty($new_file)) {
      $reference = $container->instanceExists($this);
    }

    if (empty($reference)) {
    
      try {
        $reference = $container->instanceCreate($this, $source);
      }
      catch (Exception $e) {
        $message = 'Failed to create instance of file ' . ($this->filename ? ($this->filename . ' ') : '');
        $message .= 'in storage container <i>' . $container->name() . '</i>.';

        if (user_access('administer storage')) {
          drupal_set_message($message);
        }

        $message .= ' <br />' . $this->logInfo();
        watchdog('storage_api', $message, NULL, WATCHDOG_WARNING, $link);
        
        throw new Exception();
      }
    }

    $this->update();

    $message = 'Generated file' . ($this->filename ? (' ' . $this->filename) : '');
    $message .= '<br />' . $this->logInfo();
    watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, $link);
    
    $container->reference = $reference;
    
    return $container;
  }

  /**
   * Serve the specified file, either directly or by an HTTP 302 redirect.
   */
  public function serve() {

    if ($this->serving_container) {
      $container = $this->serving_container;
    }
    else {
      $container = $this->servingContainer();
    
      if (!$container) {
        $container = $this->generate();
      }
        
      $this->cacheServingContainer($container);
    }
    
    if ($container) {
      $this->logServing($container->container_id);
      
      // this may not return
      $url = $container->serviceInstanceServe($container->reference);
    }
    elseif ($this->options['serve_source_url'] && $this->source_url) {

      $this->logServing();
      $url = $this->source_url;
    }
    
    if (!isset($url)) {
      _storage_service_unavailable();
      return;
    }
    
    // this will definitely not return
    drupal_goto($url);    // 302 redirect (don't cache)
  }


  /**
   * Generates a URL that will serve the supplied file.
   *
   * If the files's class is set to serve by redirection, then the URL will be on your site. Otherwise, Storage API will determine which container to serve from and generate a direct URL.
   *
   * @param $absolute
   *   Whether the generated URL should be absolute.
   * @return
   *   The generated URL.
   */
  public function serveURL($absolute = FALSE) {

    if ($this->options['redirect'] || ($this->serving_container === FALSE)  // FALSE means there is no container.
      || !$this->access())
    {
      return url($this->path(), array('absolute' => $absolute));
    }

    if ($this->serving_container) {
      $container = $this->serving_container;
    }
    else {
      $container = $this->servingContainer();
      $this->cacheServingContainer($container);
    }
    
    if ($container) {

      $url = $container->instanceServeURL($container->reference);
    }
    elseif ($this->options['serve_source_url'] && $this->source_url) {

      $url = $this->source_url;
    }
    
    if (!isset($url)) {
      $url = $this->path();
    }
    else {
      $this->touch();
    }

    return url($url, array('absolute' => $absolute));
  }


  public function path() {
    return 'system/storage/serve/' . $this->storage_id . ($this->filename ? ('/' . $this->filename) : '');
  }


  /**
   * Returns information required to stream a file with RTMP.
   *
   * Not all services support RTMP.
   *
   * @return
   *   An associative array of information to stream the file with RTMP:
   *     - 'streamer'
   *     - 'file'
   */
  public function serveRTMP() {
    
    if ($this->serving_container === FALSE) {  // FALSE means there is no container
      return FALSE;
    }
    
    if ($this->serving_container) {
      $container = $this->serving_container;
    }
    else {
      $container = $this->servingContainer();
      $this->cacheServingContainer($container);
    }
    
    if (!$container)
      return FALSE;
    
    if (!$this->access()) {
      return FALSE;
    }
    
    return $container->serviceInstanceServe($container->reference);
  }


  /**
   * Transfers serving statistics from one file to another.
   *
   * @param $old_storage_id
   *   storage_id of the file the statistics are being transferred from.
   */
  public function inheritServings($old_storage_id) {

    if (!$old_storage_id)
      return;

    db_update('storage_serving')
      ->fields(array('storage_id' => $this->storage_id))
      ->condition('storage_id', $old_storage_id)
      ->execute();

    $old_stats = db_select('storage')
      ->fields('storage', array('servings', 'last_touched'))
      ->condition('storage_id', $old_storage_id)
      ->execute()
      ->fetchAssoc();

    db_update('storage')
      ->expression('servings', 'servings + :servings', array(':servings' => $old_stats['servings']))
      ->expression('last_touched', 'GREATEST(last_touched, :last_touched)', array(':last_touched' => $old_stats['last_touched']))
      ->condition('storage_id', $this->storage_id)
      ->execute();
  }

  public function logInfo() {

    $info = $this->module ? ('module: ' . $this->module . ', ') : '';
    $info .= $this->type ? ('type: ' . $this->type . ', ') : '';
    $info .= 'class: ' . $this->name . ', <br />';
    
    $info .= 'storage_id: ' . $this->storage_id;
    $info .= $this->file_id ? (', file_id: ' . $this->file_id) : '';
    $info .= $this->size ? (', size: ' . storage_format_size($this->size)) : '';

    return $info;
  }


  public function fileLogInfo() {

    $info = 'file_id: ' . $this->file_id . ', ';
    $info .= 'size: ' . $this->size();

    return $info;
  }
  
  
  public function size() {
    return storage_format_size($this->size);
  }


  /**
   * Schedule a file for complete removal from the system.
   *
   * All instances of the file will be removed during cron.
   */
  public function remove() {

    db_update('storage')
      ->fields(array(
        'check_me' => 1,
        'serving_container' => serialize(FALSE),
      ))
      ->expression('selector_id', 'NULL')
      ->condition('storage_id', $this->storage_id)
      ->execute();
    
    $message = 'File removed' . ($this->filename ? (': ' . $this->filename . ' ') : '') . ' <br />';
    $message .= $this->logInfo();
    
    watchdog('storage_api', $message, NULL);
  }


  /**
   * Generate HTTP headers for serving a specific file.
   *
   * The headers will let the file will be cached for 1 year, so URLs must not ever be reused.
   *
   * @return
   *   An array of HTTP headers.
   */
  public function HTTPHeaders($acsc = FALSE) {
    $headers[] = 'Content-Type: ' . $this->mimetype;
    $headers[] = 'Content-Length: ' . $this->size;
    $headers[] = 'Content-MD5: ' . base64_encode($this->md5);
    $headers[] = 'Last-Modified: ' . date(DATE_RFC1123, $this->obtained);
    $headers[] = 'Content-Disposition: attachment; filename="' . $this->filename . '"';
    
    if ($acsc) {
      $headers[] = 'Cache-control: max-age=' . 60 * 60 * 24 * 365;  // the file will stay fresh for a whole year 
    }

    return $headers;
  }


  /**
   * Output directly the correct HTTP headers for a specific file.
   */
  public function SetHTTPHeaders() {

    // Unset the headers we don't want (they still get sent unfortunately).
    header('Set-Cookie:');
    header('Expires:');
    
    foreach ($this->HTTPHeaders() as $header) {
      header($header);
    }
  }


  private function acquireFromSourceURL() {
    $time = time();

    if ($time < $this->data['next_acquire_time'])
      return;
      
    $filepath = _storage_acquire_from_url($this->source_url);
    
    if ($filepath) {
      $file_id = _storage_api_file_id($filepath, basename(parse_url($this->source_url, PHP_URL_PATH)), $new_file);

      if (!$this->file_id) {
        $this->file_id = $file_id;
      }
      
      if ($this->file_id != $file_id) {    // the file has changed

        _storage_delete_file_if_unrequired($file_id);
        @unlink($filepath);
        $filepath = FALSE;
      }
      else {
        unset($this->data['next_acquire_time']);
        unset($this->data['next_acquire_delay']);

        $this->obtained = $time;
        $this->update();
        
        // we need to reload $storage to get the filesize, etc
//          $this = storage_load($this->storage_id);
        $this->new_file = $new_file;    // _storage_ensure_instance_exists() will make use of this

        $message = 'Acquired file from url: ' . $this->source_url . ' . <br />';
        $message .= $this->logInfo();
        
        $link = $this->nid ? l(t('view'), 'node/' . $this->nid) : NULL;
      
        watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, $link);

        return $filepath;
      }
    }
    
    if ($filepath === FALSE) {   // hard failure

      unset($this->data['next_acquire_time']);
      unset($this->data['next_acquire_delay']);
      unset($this->source_url);
      
      $this->update();

      $message = 'Failed acquiring file from url: ' . $this->source_url . ' . <br />';
      $message .= $this->logInfo();
      
      $link = $this->nid ? l(t('view'), 'node/' . $this->nid) : NULL;
      
      watchdog('storage_api', $message, NULL, WATCHDOG_WARNING, $link);

      return FALSE;
    }
    
    
    // soft failure
    
    if ($this->data['next_acquire_time']) {
    
      $this->data['next_acquire_time'] += $this->data['next_acquire_delay'];
      $this->data['next_acquire_delay'] *= 2;
    }
    else {
      $this->data['next_acquire_time'] = $time + 60;
      $this->data['next_acquire_delay'] = 120;
    }
    
    $this->update();
  }


  /**
   * Get a filepath that can be used to access a file locally.
   *
   * The file will be made available locally (this may involve downloading it or generating it). If $keep_me is set to TRUE, then the filepath must not be deleted / modified. If it is set to FALSE, then it must be deleted with unlink() after use.
   *
   * @param $keep_me
   *   Variable to receive the keep_me status of the returned filepath.
   * @return
   *   A filepath where the file can be accessed.
   */
  public function getLocalFilepath(&$keep_me) {

    $query = db_select('storage_instance');
    $query->join('storage_class_container', NULL, 'storage_instance.container_id = storage_class_container.container_id');
    $query->join('storage_selector', NULL, 'storage_class_container.class_id = storage_selector.class_id');
    $query->join('storage_container', NULL, 'storage_instance.container_id = storage_container.container_id');
    
    $row = $query->condition('storage_instance.file_id', $this->file_id)
      ->condition('storage_selector.selector_id', $this->selector_id)
      ->condition('storage_container.suspended', 0)
      ->fields('storage_instance')
      ->fields('storage_container')
      ->orderBy('storage_selector.migrating')
      ->orderBy('storage_class_container.weight', 'DESC')
      ->orderBy('storage_class_container.container_id')
      ->range(0, 1)
      ->execute()
      ->fetchAssoc();
      
    if ($row) {
      $container = storage_container_new($row);
    }
    else {
      $container = $this->initialContainerInstance();
    }
    
    if ($container) {
    
      $filepath = $container->serviceInstanceGetFilepath($container->reference);
      $keep_me = $container->service()->info['local'];
      return $filepath;
    }
    
    if ($this->source_url) {

      $filepath = $this->acquireFromSourceURL();

      if ($filepath) {
        return $filepath;
      }
    }

    if ($this->module) {

      try {
        list($filepath, $filename) = module_invoke($this->module, 'storage_generate', $this);
      }
      catch (Exception $e) {
        return FALSE;
      }
      
      $file_id = _storage_api_file_id($filepath, $filename, $new_file);
      
      if (!$file_id) {
        @unlink($filepath);
        return FALSE;
      }
      
      if ($this->file_id && ($this->file_id != $file_id)) {
      
        // The storage's file_id is changing. If there are any instances of the
        // old file, create a dummy storage so these instances can be destroyed
        // if necessary.
        $count = db_select('storage_instance')
          ->condition('file_id', $this->file_id)
          ->countQuery()
          ->execute()
          ->fetchField();
          
        if ($count > 0) {
          
          db_insert('storage')
            ->fields(array(
              'file_id' => $this->file_id,
              'obtained' => $this->obtained,
              'check_me' => 1,
              'data' => '',
            ))
            ->execute();
        }
      }

      if ($this->file_id != $file_id) {
        $this->file_id = $file_id;
        $this->obtained = time();
        $this->update();

        // we need to reload $storage to get the filesize, etc
        $storage = storage_load($this->storage_id);
        
        foreach ($storage as $key => $value) {
          $this->$key = $value;
        }
        
        $this->new_file = $new_file;    // StorageContainer::ensureInstanceExists() will make use of this
      }
            
      return $filepath;
    }

    return FALSE;
  }


  public function cacheServingContainer($container) {

    $this->serving_container = serialize($container);

    db_update('storage')
      ->fields(array('serving_container' => $this->serving_container))
      ->condition('storage_id', $this->storage_id)
      ->execute();
  }


  public function flushServingContainer() {

    db_update('storage')
      ->expression('serving_container', 'NULL')
      ->condition('storage_id', $this->storage_id)
      ->execute();
      
    unset($this->serving_container);
  }
  
  public function check() {

    // Get all containers this storage is supposed to have an instance in.
    $query = db_select('storage', NULL, array('fetch' => PDO::FETCH_ASSOC));
    $query->join('storage_selector', NULL, 'storage.selector_id = storage_selector.selector_id');
    $query->join('storage_class', NULL, 'storage_selector.class_id = storage_class.class_id');
    $query->join('storage_class_container', NULL, 'storage_class.class_id = storage_class_container.class_id');
    $query->join('storage_container', NULL, 'storage_class_container.container_id = storage_container.container_id');
    
    $result = $query->fields('storage_container')
      ->condition('storage.storage_id', $this->storage_id)
      ->condition('storage_selector.migrating', 0)
      ->orderBy('storage_class_container.weight')
      ->orderBy('storage_container.container_id')
      ->execute();
    
    $container_ids = array();
    
    $failure = FALSE;
    
    foreach ($result as $fields) {
      $container = storage_container_new($fields);
      $exists = $container->ensureInstanceExists($this);
      
      if (!$exists) {
        $failure = TRUE;
      }
      
      $container_ids[] = $container->container_id;
    }
    
    if ($failure) {
      return FALSE;
    }
  
    // The storage is fully propagated, now we can destroy unrequired instances.
    if (!$this->destroyUnrequiredInstances($container_ids)) {
      return FALSE;
    }
    
    if ($this->selector_id) {
      $this->check_me = FALSE;
      $this->update();
    }
    else {
      // This storage can now be deleted.
      db_delete('storage')
        ->condition('storage_id', $this->storage_id)
        ->execute();

      _storage_delete_file_if_unrequired($this->file_id);
    }
    
    return TRUE;
  }

  
  public function destroyUnrequiredInstances($container_ids) {

    $query = db_select('storage_instance', NULL, array('fetch' => PDO::FETCH_ASSOC));
    $query->join('storage_container', NULL, 'storage_instance.container_id = storage_container.container_id');

    $query->fields('storage_container')
      ->fields('storage_instance', array('reference'))
      ->condition('storage_instance.file_id', $this->file_id)
      ->orderBy('storage_container.container_id');

    if (count($container_ids)) {
      $query->condition('storage_instance.container_id', $container_ids, 'NOT IN');
    }

    $failure = FALSE;

    foreach ($query->execute() as $row) {
      $reference = $row['reference'];
      $container = storage_container_new($row);
      
      $success = $container->instanceDestroyIfUnrequired($this, $reference);
      
      if (!$success)
        $failure = TRUE;
    }
    
    return !$failure;
  }


  public function copyCandidateContainers(StorageContainer $container) {

    // Find all the containers in $storage's class that are using $container's service, 
    // except $container.
    $query = db_select('storage_selector', NULL, array('fetch' => PDO::FETCH_ASSOC));
    $query->join('storage_class_container', NULL, 'storage_selector.class_id = storage_class_container.class_id');
    $query->join('storage_container', NULL, 'storage_class_container.container_id = storage_container.container_id');
    
    $result = $query->fields('storage_container', array('container_id'))
      ->condition('storage_selector.selector_id', $this->selector_id)
      ->condition('storage_class_container.container_id', $container->container_id, '!=')
      ->condition('storage_container}.service_id', $container->service_id)
      ->orderBy('storage_class_container.weight')
      ->orderBy('storage_container.container_id')
      ->execute();
    
    $candiate_container_ids = array();
    
    foreach ($result as $candiate_container) {
      $candiate_container_ids[] = $candiate_container['container_id'];
    }
    
    return $candiate_container_ids;
  }
}

