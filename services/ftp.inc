<?php
// $Id$

// Copyright 2009-2010 Jonathan Brown


class StorageFTP extends StorageContainer implements StorageServiceInterface {

  private $ftp;

  private function login($settings) {
  
    if (!empty($this->ftp)) {
      return;
    }

    $this->ftp = @ftp_connect($settings['host'], $settings['port'] ? $settings['port'] : NULL);
    
    if (!$this->ftp) {
      throw new Exception();
    }
    
    if (!@ftp_login($this->ftp, $settings['username'], $settings['password'])) {
      @ftp_close($this->ftp);
      throw new Exception();
    }
    
    @ftp_pasv($this->ftp, TRUE);
  }

  public function __destruct() {
  
    if (!empty($this->ftp)) {
      @ftp_close($this->ftp);
    }
  }
   
  public function serviceSettingsForm($edit = FALSE) {

    $items['host'] = array(
      '#type' => 'textfield',
      '#title' => t('Host'),
      '#description' => t('The FTP server address. This parameter shouldn\'t have any trailing slashes and shouldn\'t be prefixed with ftp:// .'),
      '#required' => TRUE,
      '#validated' => TRUE,
      '#default_value' => isset($this->settings['host']) ? $this->settings['host'] : NULL,
    );
    
    $items['port'] = array(
      '#type' => 'textfield',
      '#title' => t('Port'),
      '#description' => t('If omitted, the default FTP port (21) will be used.'),
      '#maxlength' => 5,
      '#size' => 5,
      '#default_value' => isset($this->settings['port']) ? $this->settings['port'] : NULL,
    );
    
    $items['username'] = array(
      '#type' => 'textfield',
      '#title' => t('Username'),
      '#description' => t(''),
      '#required' => TRUE,
      '#validated' => TRUE,
      '#default_value' => isset($this->settings['username']) ? $this->settings['username'] : NULL,
    );

    $items['password'] = array(
      '#type' => 'password',
      '#title' => t('Password'),
    );

    $items['directory'] = array(
      '#type' => 'textfield',
      '#title' => t('Directory'),
      '#description' => t('The directory to be created on the FTP server.'),
      '#required' => TRUE,
      '#validated' => TRUE,
      '#default_value' => isset($this->settings['directory']) ? $this->settings['directory'] : NULL,
    );
    
    $items['permissions'] = array(
      '#type' => 'textfield',
      '#title' => t('File permissions'),
      '#description' => t('Octal format, e.g. 644.<br />For serving, ensure the files are readable by the web server.<br />If omitted, the FTP server\'s default permissions will be used.'),
      '#maxlength' => 4,
      '#size' => 5,
      '#default_value' => isset($this->settings['permissions']) ? $this->settings['permissions'] : NULL,
    );
    
    if ($this->capability) {

      $items['base_url'] = array(
        '#type' => 'textfield',
        '#title' => t('Base URL'),
        '#description' => t('The URL of the directory specified above.'),
        '#default_value' => isset($this->settings['base_url']) ? $this->settings['base_url'] : NULL,
      );

      $items['base_url_rtmp'] = array(
        '#type' => 'textfield',
        '#title' => t('Base RTMP URL'),
        '#description' => t('The RTMP URL of the directory specified above.<br />This is for Flash media streaming.'),
        '#default_value' => isset($this->settings['base_url_rtmp']) ? $this->settings['base_url_rtmp'] : NULL,
      );
    }

    return $items;
  }
  
  public function serviceSettingsValidate($edit = FALSE) {

    if (!$this->settings['host']) {
      form_set_error('settings][host', t('Host field is required.'));
    }

    if (!$this->settings['username']) {
      form_set_error('settings][username', t('Username field is required.'));
    }
    
    if (!$this->settings['directory']) {
      form_set_error('settings][directory', t('Password field is required.'));
    }

    if (form_get_errors())
      return;

    try {
      _storage_ftp_login($this->settings);
    }
    catch (Exception $e) {
      form_set_error('settings', t('Failed to authenticate.'));
    }
  }
  
  public function serviceCreate() {
    _storage_ftp_login($this->settings);
    
    $path = drupal_get_path('module', 'storage_api');

    $success = ftp_mkdir($this->ftp, $this->settings['directory']);
    
    if ($success)
      $success = ftp_chdir($this->ftp, $this->settings['directory']);

    if ($success)
      $success = ftp_put($this->ftp, '.htaccess', $path . '/files.htaccess', FTP_BINARY);

    if ($success && $this->settings['permissions'])
      $success = ftp_site($this->ftp, 'chmod ' . $this->settings['permissions'] . ' .htaccess');

    if (!$success) {
      throw new Exception();
    }
  }
  
  public function serviceInfo() {

    $info = array(
      t('Host') => $this->settings['host'],
      t('Port') => $this->settings['port'] ? $this->settings['port'] : 21,
      t('Username') => $this->settings['username'],
      t('Directory') => $this->settings['directory'],
    );
    
    if ($this->settings['permissions'])
      $info[t('File permissions')] = $this->settings['permissions'];

    if ($this->capability)
      $info[t('Base URL')] = url($this->settings['base_url'], array('absolute' => TRUE));

    return $info;
  }
  
  public function serviceDestroy() {}
  
  public function serviceInstanceCreate(Storage $storage, array $source) {
    _storage_ftp_login($this->settings);
    
    
    // File instances need to use the actual filename so the webserver serves them
    // with their filename in the Content-Disposition http header.
    
    // This means we cannot use the Cache-control max-age http header
    // as URLs may be reused.
    
    $filename = $this->uniqueFilename($source['filename']);
    
    $success = @ftp_chdir($this->ftp, $this->settings['directory']);
    
    if ($success)
      $success = @ftp_put($this->ftp, $filename, $source['filepath'], FTP_BINARY);
    
    if ($success && $this->settings['permissions'])
      $success = @ftp_site($this->ftp, 'chmod ' . $this->settings['permissions'] . ' ' . $filename);

    if ($success)
      return $filename;
  }
  
  public function serviceInstanceExists($reference) {
    return FALSE;
  }
  
  public function serviceInstanceCheck($reference) {
  }
  
  public function serviceInstanceDestroy($reference) {

    _storage_ftp_login($this->settings);
      
    $result = @ftp_delete($this->ftp, $this->settings['directory'] . $reference);
    
    return $result;
  }
  
  public function serviceInstanceGetFilepath($reference) {

    _storage_ftp_login($this->settings);
    
    $filepath = tempnam(file_directory_temp(), '');
    $result = @ftp_get($this->ftp, $filepath, $this->settings['directory'] . $reference, FTP_BINARY);
    
    if ($result)
      return $filepath;

    @unlink($filepath);
    return FALSE;
  }
  
  public function serviceInstanceServe($reference) {

    return $this->settings['base_url'] . $reference;
  }
}


/**
 * Implement hook_storage_object_instance_create() .
 */
function storage_ftp_storage_object_instance_create(Storage $storage, array $source) {
  
  $this->ftp = _storage_ftp_login($this->settings);
  
  if (!$this->ftp)
    return FALSE;
    
  $filename = $storage->object_id;

  
  // We need the filename to have an extension that matches its mime type
  // so the webserver serves the file with the correct mime type.

  $extension = storage_api_mime_extension($storage->mimetype);
  
  if ($extension)
    $filename .= '.' . $extension;
  
  $success = @ftp_chdir($this->ftp, $this->settings['directory'] . '/objects');
  
  if ($success)
   $success = @ftp_put($this->ftp, $filename, $source['filepath'], FTP_BINARY);
   
  if ($success && $this->settings['permissions'])
    $success = @ftp_site($this->ftp, 'chmod ' . $this->settings['permissions'] . ' ' . $filename);
  
  @ftp_close($this->ftp);
  
  if ($success)
    return $filename;
}


/**
 * Implement hook_storage_instance_serve_rtmp() .
 */
function storage_ftp_storage_instance_serve_rtmp(Storage $storage, $reference) {

  if ($this->settings['base_url_rtmp']) {
  
    return array(
      'streamer' => $this->settings['base_url_rtmp'],
      'file' => $reference,
    );
  }

  return FALSE;
}

