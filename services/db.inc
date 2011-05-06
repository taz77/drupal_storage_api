<?php

// Copyright 2008-2010 Jonathan Brown


class StorageDB extends StorageContainer implements StorageServiceInterface {

  public function serviceSettingsForm($edit = FALSE) {
    global $db_url;
    
    if (is_array($db_url)) {
    
      foreach ($db_url as $key => $url)
        $options[$key] = $key;
    
      $items['db'] = array(
        '#type' => 'select',
        '#title' => t('Database'),
        '#options' => $options,
        '#required' => TRUE,
        '#validated' => TRUE,
        '#default_value' => isset($this->settings['db']) ? $this->settings['db'] : NULL,
      );
    }
    else {
      $items['#db'] = 'default';
    }

    $items['table'] = array(
      '#type' => 'textfield',
      '#title' => t('Table'),
      '#description' => t('Without prefix or {}, e.g. \'storage_contents\'.'),
      '#required' => TRUE,
      '#validated' => TRUE,
      '#default_value' => isset($this->settings['table']) ? $this->settings['table'] : NULL,
    );
    
    return $items;
  }
  
  public function serviceSettingsValidate($edit = FALSE) {

    if (!$this->settings['table']) {
      form_set_error('settings][path', t('Database table field is required.'));
    }
  }
  
  public function serviceCreate() {

    db_set_active($this->settings['db']);
    
    $success = db_query("
      CREATE TABLE {%s} (
        object_id INT UNSIGNED NOT NULL,
        contents LONGBLOB NOT NULL,
        PRIMARY KEY (object_id)
      )
    ",
      $this->settings['table']
    );
    
    db_set_active();
  }
  
  public function serviceInfo() {

    if ($this['settings']['db'])
      $info[t('Database')] = $this->settings['db'];
      
    $info[t('Table')] = $this->settings['table'];

    return $info;
  }
  
  public function serviceDestroy() {
    
    db_set_active($this->settings['db']);

    $success = db_query("  
      DROP TABLE {%s}
    ",
      $this->settings['table']
    );
    
    db_set_active();
    
    return $success;
  }
  
  public function serviceInstanceCreate(Storage $storage, array $source) {
    
    db_set_active($this->settings['db']);
    
    $success = db_query("
      INSERT INTO {%s}
      SET object_id = %d,
        contents = 0x%s
    ",
      $this->settings['table'],
      $storage->object_id,
      bin2hex(file_get_contents($source['filepath']))
    );
    
    db_set_active();
    
    return $success;
  }
  
  public function serviceInstanceCopy(Storage $storage, $reference, $source_container){}
  
  public function serviceInstanceCheck($reference) {
  }
  
  public function serviceInstanceDestroy($reference) {
    
    db_set_active($this->settings['db']);
    
    $success = db_query("
      DELETE FROM {%s}
      WHERE object_id = %d
    ",
      $this->settings['table'],
      $reference
    );

    db_set_active();
    
    return $success;
  }
  
  public function serviceInstanceGetFilepath($reference) {
    
    $filepath = tempnam(file_directory_temp(), '');
    
    db_set_active($this->settings['db']);

    $success = file_put_contents($filepath,
      db_result(db_query("
        SELECT contents
        FROM {%s}
        WHERE object_id = %d
      ",
        $this->settings['table'],
        $reference
      ))
    );

    db_set_active();
    
    if (!$success) {
      @unlink($filepath);
      return FALSE;
    }

    return $filepath;
  }
  
  public function serviceInstanceServe($reference) {
    $storage->SetHTTPHeaders();
    
    db_set_active($this->settings['db']);

    echo db_result(db_query("
      SELECT contents
      FROM {%s}
      WHERE object_id = %d
    ",
      $this->settings['table'],
      $reference
    ));
    
    db_set_active();
    drupal_exit();
  }
}

