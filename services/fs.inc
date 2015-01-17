<?php

/**
 * @file
 * FS Storage class files for Storage API.
 */

/**
 * Provides a file system storage container.
 */
class StorageFS extends StorageContainer implements StorageContainerInterface {

  /**
   * {@inheritdoc}
   */
  public function serviceSettingsDefault() {
    return array(
      'path' => '',
      'base_url' => '',
    );
  }

  /**
   * {@inheritdoc}
   */
  public function serviceSettingsForm($already_exists) {
    if (!$this->settings['path']) {
      $this->settings['path'] = storage_new_local_storage_path();
    }

    $items['path'] = array(
      '#type' => 'textfield',
      '#title' => t('Directory path'),
      '#description' => t('Can be absolute or relative.'),
      '#default_value' => $this->settings['path'],
      '#required' => TRUE,
      '#validated' => TRUE,
    );

    if ($already_exists) {
      $items['path']['#description'] .= '<br />' .
        t('If this setting is changed, the directory must be moved manually to the new path.');
    }

    if (!$this->access_control) {

      if (!$this->settings['base_url']) {
        $this->settings['base_url'] = $this->settings['path'];
      }

      $items['base_url'] = array(
        '#type' => 'textfield',
        '#title' => t('Base URL'),
        '#description' => t("The URL of the directory path specified above (typically exactly the same). Can be absolute or relative."),
        '#required' => TRUE,
        '#validated' => TRUE,
        '#default_value' => $this->settings['base_url'],
      );
    }

    return $items;
  }

  /**
   * {@inheritdoc}
   */
  public function serviceSettingsValidate($already_exists) {
    if (!$this->settings['path']) {
      form_set_error('settings][path', t('Directory path field is required.'));
      return;
    }

    if (!$already_exists && file_prepare_directory($this->settings['path'], 0)) {
      form_set_error('settings][path', t('Directory already exists.'));
    }

    if ($already_exists && !file_prepare_directory($this->settings['path'], 0)) {
      form_set_error('settings][path', t('Directory does not exist.'));
    }

    if (!$this->access_control && !$this->settings['base_url']) {
      form_set_error('settings][base_url', t('Base URL field is required.'));
    }
  }

  /**
   * {@inheritdoc}
   */
  public function serviceCreate() {
    if (!file_prepare_directory($this->settings['path'], FILE_CREATE_DIRECTORY | FILE_MODIFY_PERMISSIONS)) {
      form_set_error('settings][path', t('The directory %directory could not be created. Please check permissions.', array('%directory' => $this->settings['path'])));

      throw new StorageException();
    }
  }

  /**
   * {@inheritdoc}
   */
  public function serviceInfo() {
    $info['Free space'] = storage_format_byte_count(@disk_free_space($this->settings['path']));
    $info['Directory'] = realpath($this->settings['path']);
    return $info;
  }

  /**
   * {@inheritdoc}
   */
  public function serviceDestroy() {
    return @rmdir($this->settings['path']);
  }

  /**
   * Gets the instance path.
   *
   * @param string $reference
   *   The path reference.
   *
   * @return string
   *   The instance path.
   */
  private function instancePath($reference) {
    return $this->settings['path'] . '/' . $reference;
  }

  /**
   * Creates the directory path if it does not exist.
   *
   * @param string $reference
   *   The path reference.
   *
   * @return string
   *   The directory path.
   */
  private function ensureDirectoryPathExists($reference) {
    $dirs = explode('/', $reference);
    $basename = array_pop($dirs);
    $path = '';

    foreach ($dirs as $dir) {
      $count = 0;
      $test_path = $path . $dir;

      while (!is_dir($this->instancePath($test_path)) &&
      !drupal_mkdir($this->instancePath($test_path), null, TRUE)) {
        $count++;
        $test_path = $path . $dir . '_' . $count;
      }

      $path = $test_path . '/';
    }

    return $path . $basename;
  }

  /**
   * {@inheritdoc}
   */
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

  /**
   * Recursively gets a list of files in the directory and any sub-directories.
   *
   * @param string $dir
   *   The directory to scan.
   *
   * @return array
   *   An array of file names.
   *
   * @throws \StorageException
   */
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

  /**
   * {@inheritdoc}
   */
  public function serviceListReferences() {

    return $this->scanDirectory($this->settings['path']);
  }

  /**
   * {@inheritdoc}
   */
  public function serviceInstanceExists($reference) {
    return @stat($this->instancePath($reference)) !== FALSE;
  }

  /**
   * {@inheritdoc}
   */
  public function serviceInstanceGetUri($reference) {
    return $this->instancePath($reference);
  }

  /**
   * {@inheritdoc}
   */
  public function serviceInstanceDestroy($reference) {

    @drupal_unlink($this->instancePath($reference));

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
