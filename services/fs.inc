<?php
// $Id$

// Copyright 2008-2010 Jonathan Brown


class StorageFS extends StorageContainer implements StorageServiceInterface {

  public function serviceSettingsForm($edit = FALSE) {

    $items['path'] = array(
      '#type' => 'textfield',
      '#title' => t('Directory path'),
      '#description' => t("Can be absolute or relative."),
      '#default_value' => isset($this->settings['path']) ? $this->settings['path'] : 
        storage_api_new_local_storage_path(),
      '#required' => TRUE,
      '#validated' => TRUE,
    );

    if ($edit) {
      $items['path']['#description'] .= '<br />' . 
        t('If this setting is changed, the directory must be moved manually to the new path.');
    }
    
    if ($this->capability) {
    
      $description  = "The URL path of the directory path specified above (typically exactly the same).<br/>";
      $description .= "Can be absolute or relative.<br />";

      $items['base_url'] = array(
        '#type' => 'textfield',
        '#title' => t('Base URL path'),
        '#description' => t($description),
        '#default_value' => isset($this->settings['base_url']) ? $this->settings['base_url'] : 
          $items['path']['#default_value'],
        '#required' => TRUE,
        '#validated' => TRUE,
      );
    }

    return $items;
  }
  
  public function serviceSettingsValidate($edit = FALSE) {

    if (!$this->settings['path']) {
      form_set_error('settings][path', t('Directory path field is required.'));
    }
    elseif (!$edit) {
      if (file_prepare_directory($this->settings['path'])) {
        form_set_error('settings][path', t('Directory already exists.'));
      }
    }
    
    if ($this->capability) {
      if (!$this->settings['base_url']) {
        form_set_error('settings][base_url', t('Base URL path field is required.'));
      }
    }
  }
  
  public function serviceCreate() {

    $storage_api_path = drupal_get_path('module', 'storage_api');

    if (!file_prepare_directory($this->settings['path'], FILE_CREATE_DIRECTORY | FILE_MODIFY_PERMISSIONS)) {
      throw new Exception();
    }

  /*
    copy($storage_api_path . '/files.htaccess', $this['settings']['path'] . '/.htaccess');
  */
  }
  
  public function serviceInfo() {

    $info['Free space'] = storage_format_size(@disk_free_space($this->settings['path']));
    $info['Directory'] = realpath($this->settings['path']);

    if ($this->capability) {
      $info['Base URL'] = url($this->settings['base_url'], array('absolute' => TRUE));
    }

    return $info;
  }
  
  public function serviceDestroy() {
  
    return @rmdir($this->settings['path']);
  }
  
  private function instancePath($reference) {
    return $this->settings['path'] . '/' . $reference;
  }
  
  private function ensurePathExists($reference) {
    $dirs = explode('/', $reference);
    $basename = array_pop($dirs);
    $path = '';
    
    foreach ($dirs as $dir) {
      $count = 0;
      $test_path = $path . $dir;

      while (!is_dir($this->instancePath($test_path)) &&
        !@mkdir($this->instancePath($test_path)))
      {
        $count++;
        $test_path = $path . $dir . '_' . $count;
      }
      
      $path = $test_path . '/';
    }
    
    return $path . $basename;
  }
  
  public function serviceInstanceCreate(Storage $storage, array $source) {

    $reference = $this->uniqueFilename($this->ensurePathExists($storage->filename));
    $path = $this->instancePath($reference);

    if (empty($source['keep_me']) && !file_uri_scheme($source['filepath'])) {  // cannot rename a file across wrapper types
      $success = @rename($source['filepath'], $path);
    }
    else {
      $success = @copy($source['filepath'], $path);
    }

    if ($success) {
      return $reference;
    }
    else {
      throw new Exception();
    }
  }
  
  private function scanDirectory($dir) {
    $files = array();
    $handle = opendir($dir);
    
    while (($filename = readdir($handle)) !== FALSE) {

      if ($filename == '.' || $filename == '..') {
        continue;
      }
    
      $filepath = $dir . '/' . $filename;
      
      if (is_dir($filepath)) {
      
        foreach ($this->scanDirectory($filepath) as $subfilename) {
          $files[] = $filename . '/' . $subfilename;
        }
      }
      else {
        $files[] = $filename;
      }
    }

    closedir($handle);
    
    return $files;
  }
  
  public function serviceManifest(&$sandbox, &$finished) {
  
    if (!isset($sandbox['files'])) {
      $sandbox['files'] = $this->scanDirectory($this->settings['path']);
    }

    $count = 100;
    $manifest = array();
    
    while ($count-- && ($reference = array_shift($sandbox['files']))) {
      $filepath = $this->instancePath($reference);
      $stat = stat($filepath);
    
      $manifest[] = array(
        'reference' => $reference,
        'size' => $stat['size'],
        'md5' => hash_file('md5', $filepath, TRUE),
      );
    }
    
    $finished = (count($sandbox['files']) == 0);
    
    return $manifest;
  }

  public function serviceInstanceExists($reference) {
    
    return @stat($this->instancePath($reference)) !== FALSE;
  }

  public function serviceInstanceCheck($reference) {
    $path = $this->instancePath($reference);
    $stat = stat($path);
    
    if ($stat === FALSE) {
      return FALSE;
    }
    
    return array(
      'size' => $stat['size'],
      'md5' => hash_file('md5', $path, TRUE),
    );
  }
  
  public function serviceInstanceDestroy($reference) {
      
    @unlink($this->instancePath($reference));
    
    $dirs = explode('/', $reference);
    array_pop($dirs);
    
    while (count($dirs)) {
    
      if (!@rmdir($this->instancePath(implode('/', $dirs)))) {
        break;
      }

      array_pop($dirs);
    }
  }
  
  public function serviceInstanceGetFilepath($reference) {

    return $this->instancePath($reference);
  }
  
  public function serviceInstanceServe($reference) {

    return file_create_url($this->settings['base_url'] . '/' . $reference);
  }
}


/*
function storage_fs_storage_instance_serve($container, $storage) {
  storage_api_set_http_headers($storage);

  @readfile($container['settings']['path'] . '/' . $container['reference']);
  exit();
}
*/

