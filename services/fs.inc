<?php

// Copyright 2008-2010 Jonathan Brown


class StorageFS extends StorageContainer implements StorageContainerInterface {

  public function serviceSettingsForm($edit = FALSE) {
    // Add defaults.
    if (!isset($this->settings['path'])) {
      $this->settings['path'] = storage_new_local_storage_path();
    }
    if (!isset($this->settings['base_url'])) {
      $this->settings['base_url'] = $this->settings['path'];
    }

    $items['path'] = array(
      '#type' => 'textfield',
      '#title' => t('Directory path'),
      '#description' => t("Can be absolute or relative."),
      '#default_value' => $this->settings['path'],
      '#required' => TRUE,
      '#validated' => TRUE,
    );

    if ($edit) {
      $items['path']['#description'] .= '<br />' . 
        t('If this setting is changed, the directory must be moved manually to the new path.');
    }
    
    if (!$this->access_control) {
      $description  = "The URL path of the directory path specified above (typically exactly the same).<br/>";
      $description .= "Can be absolute or relative.";

      $items['base_url'] = array(
        '#type' => 'textfield',
        '#title' => t('Base URL path'),
        '#description' => t($description),
        '#default_value' => $this->settings['base_url'],
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
  }
  
  public function serviceCreate() {

    $storage_path = drupal_get_path('module', 'storage');

    if (!file_prepare_directory($this->settings['path'], FILE_CREATE_DIRECTORY | FILE_MODIFY_PERMISSIONS)) {
      form_set_error('settings][path', t('The directory %directory could not be created. Please check permissions.',
        array('%directory' => $this->settings['path'])));
      throw new StorageException();
    }
  }
  
  public function serviceInfo() {

    $info['Free space'] = storage_format_size(@disk_free_space($this->settings['path']));
    $info['Directory'] = realpath($this->settings['path']);

    if (!$this->access_control) {
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
  
  private function ensureDirectoryPathExists($reference) {
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
  
  public function serviceInstanceCreate(Storage $storage, $uri) {

    $reference = $this->uniqueFilename($this->ensureDirectoryPathExists($storage->filename));
    $path = $this->instancePath($reference);

    $success = copy($uri, $path);

    if ($success) {
      return $reference;
    }
    else {
      throw new StorageException();
    }
  }
  
  private function scanDirectory($dir) {
    $files = array();
    $handle = opendir($dir);
    
    if (!$handle) {
      throw new StorageException();
    }
    
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
  
  public function serviceListReferences() {
  
    return $this->scanDirectory($this->settings['path']);
  }

  public function serviceInstanceExists($reference) {
    
    return @stat($this->instancePath($reference)) !== FALSE;
  }

  public function serviceInstanceGetUri($reference) {

    return $this->instancePath($reference);
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
}

