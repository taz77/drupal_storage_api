<?php

// Copyright 2009-2010 Jonathan Brown


class StorageRackspace extends StorageContainer implements StorageServiceInterface {

  private $conn;

  private function log_exception($e, $dsm = TRUE) {

    watchdog('storage_rackspace', 'php-cloudfiles exception: @msg', array('@msg' => $e->getMessage()), WATCHDOG_ERROR);
    
    if ($dsm && user_access('administer storage'))
      drupal_set_message(t('php-cloudfiles exception: @msg', array('@msg' => $e->getMessage())), 'error');
  }

  private function init($settings) {

    $path = drupal_get_path('module', 'storage_rackspace');
    require_once DRUPAL_ROOT . '/' . $path . '/php-cloudfiles/cloudfiles.php';

    if (!class_exists('CF_Authentication')) {
      watchdog('storage_rackspace', 'php-cloudfiles not found', array(), WATCHDOG_ERROR);
      
      if (user_access('administer storage')) {
        drupal_set_message(t('php-cloudfiles not found. Check the <a href="!path">status report</a>.', 
          array('!path' => url('admin/reports/status'))), 'error');
      }
      
      throw new Exception();
    }

    $auth = new CF_Authentication($settings['username'], $settings['api_key']);
    $auth->ssl_use_cabundle();

    try {
      $auth->authenticate();
    }
    catch(AuthenticationException $e) {
      _storage_rackspace_log_exception($e, FALSE);
      
      if (user_access('administer storage'))
        drupal_set_message(t('Invalid username or access key.'));
        
      throw new Exception();
    }
    catch(Exception $e) {
      _storage_rackspace_log_exception($e);
      return FALSE;
    }

    $this->conn = new CF_Connection($auth);
    $this->conn->ssl_use_cabundle();
  }

  private function rscf_container() {
    $rscf_containers = &drupal_static(__FUNCTION__);
    $rscf_containers = array();
    
    if ($rscf_containers[$this->container_id])
      return $rscf_containers[$this->container_id];

    $this->init($this->settings);
    
    try {
      $rscf_container = $this->conn->get_container($this->settings['rscf_container']);
    }
    catch(Exception $e) {
      $this->log_exception($e);
      throw new Exception();
    }

    $rscf_containers[$this->container_id] = $rscf_container;

    return $rscf_container;
  }
  
  public function serviceCheck() {

    $path = drupal_get_path('module', 'storage_api');
    @include_once DRUPAL_ROOT . '/' . $path . '/services/php-cloudfiles/cloudfiles.php';

    if (class_exists('CF_Authentication')) {
      return;
    }
    
    $message = t('Download <a href="http://github.com/rackspace/php-cloudfiles/downloads" target="_blank">php-cloudfiles</a>, decompress it, rename the directory to <em>php-cloudfiles</em> and put it in %directory.', array('%directory' => $path . '/services')) . '<br />';
    $message .= t('Alternatively you can use <a href="http://git-scm.com/" target="_blank">Git</a>:') . '<br />';
    $message .= '<code>git clone git://github.com/rackspace/php-cloudfiles.git</code>';
    
    throw new Exception($message);
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
    
    if (form_get_errors())
      return;
    
    try {
      $this->init($this->settings);
    }
    catch (Exception $e) {
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
    
    if ($rscf_container) {
      form_set_error('settings][rscf_container', t('Cloud Files container %name already exists.', 
        array('%name' => $this['settings']['rscf_container'])));
    }
    
    try {
      $rscf_container = $this->conn->create_container($this->settings['rscf_container']);
    
      if ($this->capability == STORAGE_API_SERVING) {
      
        $rscf_container->make_public(60 * 60 * 24 * 3);    // 3 days is the maximum ttl that can be set in the api
        $this->settings['cdn_uri'] = $rscf_container->cdn_uri;
      }
      else {
        $rscf_container->make_private();    // strangely, this has to be done explicity, or it might be public
      }
    }  
    catch (Exception $e) {
      $this->log_exception($e);
      throw new Exception();
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
      _storage_rackspace_log_exception($e);
      throw new Exception();
    }
  }
  
  public function serviceInstanceCreate(Storage $storage, array $source) {

    $name = $storage->file_id . '/' . $storage->filename;
    
    try {
      $rscf_container = $this->rscf_container($this);
    }
    catch (Exception $e) {
      throw new Exception();
    }

    try {
      $object = $rscf_container->create_object($name);
      $object->content_type = $storage->mimetype;
      $object->set_etag(base64_encode($storage->md5));
      $object->load_from_filename($source['filepath'], FALSE);

      $object->metadata = array(
        'file_id' => $storage->file_id,
        'object_id' => $storage->object_id,
        'filename' => $storage->filename,
      );

      $object->sync_metadata();
    }
    catch(Exception $e) {
      log_exception($e);

      try {
        $rscf_container->delete_object($name);
      }
      catch(Exception $e) {}

      throw new Exception();
    }
  }
  
  public function serviceInstanceCheck($reference) {
  }
  
  public function serviceInstanceDestroy($reference) {

    $rscf_container = $this->rscf_container($this);
      
    try {
      $rscf_container->delete_object($reference);
    }
    catch(Exception $e) {
      _storage_rackspace_log_exception($e);
      throw new Exception();
    }
  }
  
  public function serviceInstanceGetFilepath($reference) {

    $rscf_container = $this->rscf_container($this);

    if (!$rscf_container)
      return FALSE;

    $filepath = tempnam(file_directory_temp(), '');
    
    try {
      $object = $rscf_container->get_object($reference);
      $object->save_to_filename($filepath);
    }
    catch(Exception $e) {
      _storage_rackspace_log_exception($e);
      @unlink($filepath);
      throw new Exception();
    }

    return $filepath;
  }
  
  public function serviceInstanceServe($reference) {

    return $this->settings['cdn_uri'] . '/'. $reference;
  }
}

