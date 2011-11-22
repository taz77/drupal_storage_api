<?php

// Copyright 2009-2011 Jonathan Brown


class StorageRackspace extends StorageContainer implements StorageContainerInterface {
  private $conn;

  private function log_exception($e, $dsm = TRUE) {

    watchdog('storage_rackspace', 'php-cloudfiles exception: @msg', array('@msg' => $e->getMessage()), WATCHDOG_ERROR);
    
    if ($dsm && user_access('administer storage')) {
      drupal_set_message(t('php-cloudfiles exception: @msg', array('@msg' => $e->getMessage())), 'error');
    }
  }

  private function init(array $settings) {

    if (module_exists('libraries')) {
      $library = libraries_load('php-cloudfiles');
    }

    if (!isset($library) || $library['loaded'] == FALSE) {
      watchdog('storage_rackspace', 'php-cloudfiles not found', array(), WATCHDOG_ERROR);
      
      if (user_access('administer storage')) {
        drupal_set_message(t('php-cloudfiles not found. Check the <a href="!path">status report</a>.', 
          array('!path' => url('admin/reports/status'))), 'error');
      }
      
      throw new StorageException();
    }

    $auth = new CF_Authentication($settings['username'], $settings['api_key']);
    $auth->ssl_use_cabundle();

    try {
      $auth->authenticate();
    }
    catch (AuthenticationException $e) {
      $this->log_exception($e, FALSE);
      
      if (user_access('administer storage')) {
        drupal_set_message(t('Invalid username or access key.'));
      }
        
      throw new StorageException();
    }
    catch (Exception $e) {
      $this->log_exception($e);
      throw new StorageException();
    }

    $this->conn = new CF_Connection($auth);
    $this->conn->ssl_use_cabundle();
  }

  private function rscf_container() {
    $rscf_containers = &drupal_static(__FUNCTION__);
    
    if (isset($rscf_containers[$this->container_id])) {
      return $rscf_containers[$this->container_id];
    }

    $this->init($this->settings);
    
    try {
      $rscf_container = $this->conn->get_container($this->settings['rscf_container']);
    }
    catch (Exception $e) {
      $this->log_exception($e);
      throw new StorageException();
    }

    $rscf_containers[$this->container_id] = $rscf_container;

    return $rscf_container;
  }

  public function serviceSettingsForm($edit = FALSE) {

    $items['credentials_info'] = array(
      '#markup' => t('Get your credentials !link.', array('!link' => l(t('here'), 
        'http://www.rackspacecloud.com/cloud_hosting_products/files', 
          array('attributes' => array('target' => '_blank')))))
    );

    $items['username'] = array(
      '#type' => 'textfield',
      '#title' => t('Username'),
      '#required' => TRUE,
      '#validated' => TRUE,
      '#default_value' => isset($this->settings['username']) ? $this->settings['username'] : NULL,
    );
    
    $items['api_key'] = array(
      '#type' => 'password',
      '#title' => t('API key'),
      '#required' => TRUE,
      '#validated' => TRUE,
    );

    if (!$edit) {

      $items['rscf_container'] = array(
        '#type' => 'textfield',
        '#title' => t('Cloud Files container'),
        '#required' => TRUE,
        '#validated' => TRUE,
        '#default_value' => isset($this->settings['rscf_container']) ? $this->settings['rscf_container'] : NULL,
      );
    }
    
    return $items;
  }
  
  public function serviceSettingsValidate($edit = FALSE) {

    if (!$this->settings['username']) {
      form_set_error('settings][username', t('Username field is required.'));
    }

    if (!$this->settings['api_key']) {
      form_set_error('settings][api_key', t('API key field is required.'));
    }
    
    if (!$this->settings['rscf_container']) {
      form_set_error('settings][rscf_container', t('Cloud Files container name field is required.'));
    }
    
    if (form_get_errors()) {
      return;
    }
    
    try {
      $this->init($this->settings);
    }
    catch (StorageException $e) {
      form_set_error('settings');
    }
  }
  
  public function serviceCreate() {
    $this->init($this->settings);
    
    try {
      $rscf_container = $this->conn->get_container($this->settings['rscf_container']);
    }
    catch (NoSuchContainerException $e) {}
    catch (Exception $e) {
      $this->log_exception($e);
      form_set_error('settings');
    }
    
    if (isset($rscf_container)) {
      form_set_error('settings][rscf_container', t('Cloud Files container %name already exists.', 
        array('%name' => $this['settings']['rscf_container'])));
    }
    
    try {
      $rscf_container = $this->conn->create_container($this->settings['rscf_container']);
    
      if ($this->access_control) {
        $rscf_container->make_private();    // strangely, this has to be done explicity, or it might be public
      }
      else {
        $rscf_container->make_public(60 * 60 * 24 * 3);    // 3 days is the maximum ttl that can be set in the api
        $this->settings['base_url'] = $rscf_container->cdn_uri;
      }
    }  
    catch (Exception $e) {
      $this->log_exception($e);
      throw new StorageException();
    }
  }
  
  public function serviceInfo() {

    $info = array(
      t('Username') => $this->settings['username'],
      t('Container') => $this->settings['rscf_container'],
    );

    return $info;
  }
  
  public function serviceDestroy() {
    $this->init($this->settings);
    
    try {
      $this->conn->delete_container($this->settings['rscf_container']);
    }
    catch (Exception $e) {
      $this->log_exception($e);
      throw new StorageException();
    }
  }
  
  public function serviceInstanceCreate(Storage $storage, $uri) {

    if ($this->enhanced_caching) {
      $reference = $storage->file_id . '/' . $storage->filename;
    }
    else {
      $reference = $this->uniqueFilename($storage->filename);
    }

    $rscf_container = $this->rscf_container();

    try {
      $object = $rscf_container->create_object($reference);
      $object->content_type = $storage->mimetype;
      $object->set_etag(base64_encode($storage->md5));
      $object->load_from_filename($uri, FALSE);

      $object->metadata = array(
        'storage_id' => $storage->storage_id,
        'file_id' => $storage->file_id,
        'filename' => $storage->filename,
      );

      $object->sync_metadata();
    }
    catch(Exception $e) {
      log_exception($e);

      try {
        $rscf_container->delete_object($reference);
      }
      catch(Exception $e) {}

      throw new StorageException();
    }

    return $reference;
  }
  
  public function serviceInstanceExists($reference) {
  
    $rscf_container = $this->rscf_container();

    try {
      $object = $rscf_container->get_object($reference);
    }
    catch(Exception $e) {
      return FALSE;
    }

    return TRUE;
  }

  public function serviceInstanceDestroy($reference) {

    $rscf_container = $this->rscf_container();
      
    try {
      $rscf_container->delete_object($reference);
    }
    catch(Exception $e) {
      $this->log_exception($e);
      throw new StorageException();
    }
  }
  
  public function serviceInstanceGetUri($reference) {

    $rscf_container = $this->rscf_container();
    $temp_uri = storage_temp_uri();
    
    try {
      $object = $rscf_container->get_object($reference);
      $object->save_to_filename($temp_uri);
    }
    catch(Exception $e) {
      $this->log_exception($e);
      throw new StorageException();
    }

    return $temp_uri;
  }
}

