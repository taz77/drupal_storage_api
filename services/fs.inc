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

    if ($this->capability)
      $info['Base URL'] = url($this->settings['base_url'], array('absolute' => TRUE));

    return $info;
  }
  
  public function serviceDestroy() {}
  
  public function serviceInstanceCreate(Storage $storage, array $source) {

    // File instances need to use the actual filename so the webserver serves them
    // with their filename in the Content-Disposition http header.
    
    // This means we cannot use the Cache-control max-age http header
    // as URLs may be reused.
    
    $filename = $this->uniqueFilename($storage->filename);
    
    $path = $this->settings['path'] . '/' . $filename;

    if (empty($source['keep_me']) && !file_uri_scheme($source['filepath']))   // cannot rename a file across wrapper types
      $success = @rename($source['filepath'], $path);
    else
      $success = @copy($source['filepath'], $path);

    if ($success) {
      return $filename;
    }
    else {
      throw Exception();
    }
  }
  
  public function serviceInstanceDestroy(Storage $storage, $reference) {
      
    @unlink($this->settings['path'] . '/' . $reference);
  }
  
  public function serviceInstanceGetFilepath(Storage $storage, $reference) {

    return $this->settings['path'] . '/' . $reference;
  }
  
  public function serviceInstanceServe(Storage $storage, $reference) {

    return $this->settings['base_url'] . '/' . $reference;
  }
}


/*
function storage_fs_storage_instance_serve($container, $storage) {
  storage_api_set_http_headers($storage);

  @readfile($container['settings']['path'] . '/' . $container['reference']);
  exit();
}
*/

