<?php

/**
 * @file
 * FTP Storage class files for Storage API.
 */

/**
 * Provides an FTP storage container.
 */
class StorageFTP extends StorageContainer implements StorageContainerInterface {

  private $ftpStream;

  /**
   * Login to the FTP server.
   *
   * @param bool $change_directory
   *   TRUE to change directory.
   *
   * @throws \StorageException
   */
  private function login($change_directory = TRUE) {

    if (isset($this->ftpStream)) {
      return;
    }

    $connect_function = $this->settings['ssl'] ? 'ftp_ssl_connect' : 'ftp_connect';
    $this->ftpStream = $connect_function($this->settings['host'], $this->settings['port'] ? $this->settings['port'] : NULL);

    if (!$this->ftpStream) {
      throw new StorageException();
    }

    if (!@ftp_login($this->ftpStream, $this->settings['username'], $this->settings['password'])) {
      ftp_close($this->ftpStream);
      throw new StorageException();
    }

    ftp_pasv($this->ftpStream, TRUE);

    if ($change_directory) {
      try {
        $this->changeDirectory($this->settings['directory']);
      }
      catch (StorageException $e) {
        ftp_close($this->ftpStream);
        throw new StorageException();
      }
    }
  }

  /**
   * Destroys the StorageFTP instance.
   */
  public function __destruct() {

    if ($this->ftpStream != NULL) {
      ftp_close($this->ftpStream);
    }
  }

  /**
   * Create the specified directory.
   *
   * @param string $directory
   *   The directory to create.
   *
   * @throws \StorageException
   */
  private function createDirectory($directory) {

    if (!ftp_mkdir($this->ftpStream, $directory)) {
      throw new StorageException();
    }
  }

  /**
   * Recursively create directories in the path.
   *
   * @param string $path
   *   The directory path.
   *
   * @throws \StorageException
   */
  private function createDirectoryRecursive($path) {
    $original_dir = ftp_pwd($this->ftpStream);

    foreach (explode('/', $path) as $directory) {
      try {
        $this->changeDirectory($directory);
      }
      catch (StorageException $e) {
        $this->createDirectory($directory);
        $this->changeDirectory($directory);
      }
    }

    $this->changeDirectory($original_dir);
  }

  /**
   * Remove the specified directory.
   *
   * @param string $directory
   *   The directory to remove.
   *
   * @throws \StorageException
   */
  private function removeDirectory($directory) {

    if (!@ftp_rmdir($this->ftpStream, $directory)) {
      throw new StorageException();
    }
  }

  /**
   * Change to the specified directory.
   *
   * @param string $directory
   *   The directory to change to.
   *
   * @throws \StorageException
   */
  private function changeDirectory($directory) {

    if (!@ftp_chdir($this->ftpStream, $directory)) {
      throw new StorageException();
    }
  }

  /**
   * Set the file mode for the file.
   *
   * @param string $filename
   *   The file name.
   * @param string $mode
   *   The file mode.
   *
   * @throws \StorageException
   */
  private function setMode($filename, $mode) {

    if (!ftp_chmod($this->ftpStream, octdec(str_pad($mode, 4, '0', STR_PAD_LEFT)), $filename)) {
      throw new StorageException();
    }
  }

  /**
   * {@inheritdoc}
   */
  public function serviceSettingsDefault() {
    return [
      'host' => '',
      'port' => '21',
      'username' => '',
      'ssl' => 0,
      'directory' => '',
      'mode' => '',
      'base_url' => '',
    ];
  }

  /**
   * {@inheritdoc}
   */
  public function serviceSettingsForm($already_exists) {

    $items['host'] = [
      '#type' => 'textfield',
      '#title' => t('Host'),
      '#description' => t("The FTP server address. This parameter shouldn't have any trailing slashes and shouldn't be prefixed with ftp:// ."),
      '#required' => TRUE,
      '#validated' => TRUE,
      '#default_value' => $this->settings['host'],
    ];

    $items['port'] = [
      '#type' => 'textfield',
      '#title' => t('Port'),
      '#description' => t('The standard port is 21.'),
      '#required' => TRUE,
      '#validated' => TRUE,
      '#maxlength' => 5,
      '#size' => 5,
      '#default_value' => $this->settings['port'],
    ];

    $items['username'] = [
      '#type' => 'textfield',
      '#title' => t('Username'),
      '#description' => '',
      '#required' => TRUE,
      '#validated' => TRUE,
      '#default_value' => $this->settings['username'],
    ];

    $items['ssl'] = [
      '#type' => 'checkbox',
      '#title' => t('SSL'),
      '#description' => t('If checked, the connection will be over SSL.'),
      '#default_value' => $this->settings['ssl'],
    ];

    $items['password'] = [
      '#type' => 'password',
      '#title' => t('Password'),
    ];

    $items['directory'] = [
      '#type' => 'textfield',
      '#title' => t('Directory'),
      '#description' => t('The directory to be created on the FTP server.'),
      '#required' => TRUE,
      '#validated' => TRUE,
      '#default_value' => $this->settings['directory'],
    ];

    $items['mode'] = [
      '#type' => 'textfield',
      '#title' => t('File mode'),
      '#description' => t("Octal format, e.g. 644.<br />For serving, ensure the files are readable by the web server.<br />If omitted, the FTP server's default permissions will be used."),
      '#maxlength' => 4,
      '#size' => 5,
      '#default_value' => $this->settings['mode'],
    ];

    if (!$this->access_control) {
      $items['base_url'] = [
        '#type' => 'textfield',
        '#title' => t('Base URL'),
        '#description' => t('The URL of the directory specified above.'),
        '#required' => TRUE,
        '#validated' => TRUE,
        '#default_value' => $this->settings['base_url'],
      ];
    }

    return $items;
  }

  /**
   * {@inheritdoc}
   */
  public function serviceSettingsValidate($already_exists) {
    if (!$this->settings['host']) {
      form_set_error('settings][host', t('Host field is required.'));
    }

    if (!$this->settings['port']) {
      form_set_error('settings][port', t('Port field is required.'));
    }

    if (!$this->settings['username']) {
      form_set_error('settings][username', t('Username field is required.'));
    }

    if (!$this->settings['directory']) {
      form_set_error('settings][directory', t('Directory field is required.'));
    }

    if (!$this->access_control && !$this->settings['base_url']) {
      form_set_error('settings][base_url', t('Base URL field is required.'));
    }

    if (form_get_errors()) {
      return;
    }

    try {
      $this->login(FALSE);
    }
    catch (StorageException $e) {
      form_set_error('settings', t('Failed to authenticate.'));
      return;
    }

    if (!$already_exists) {
      try {
        $this->changeDirectory($this->settings['directory']);
      }
      catch (StorageException $e) {
        return;
      }
      form_set_error('settings][directory', t('Directory already exists.'));
    }
    else {
      try {
        $this->changeDirectory($this->settings['directory']);
      }
      catch (StorageException $e) {
        form_set_error('settings][directory', t('Directory does not exist.'));
      }
    }
  }

  /**
   * {@inheritdoc}
   */
  public function serviceCreate() {
    $this->login(FALSE);
    $this->createDirectoryRecursive($this->settings['directory']);
  }

  /**
   * {@inheritdoc}
   */
  public function servicePostCreate() {
    $this->changeDirectory($this->settings['directory']);
  }

  /**
   * {@inheritdoc}
   */
  public function serviceInfo() {
    $info = [
      t('Host') => $this->settings['host'],
      t('Port') => $this->settings['port'] ? $this->settings['port'] : 21,
      t('Username') => $this->settings['username'],
      t('SSL') => $this->settings['ssl'] ? t('Yes') : t('No'),
      t('Directory') => $this->settings['directory'],
    ];

    if ($this->settings['mode']) {
      $info[t('File mode')] = $this->settings['mode'];
    }

    if ($this->settings['base_url'] !== '') {
      $info[t('Base URL')] = url($this->settings['base_url'], ['absolute' => TRUE]);
    }

    return $info;
  }

  /**
   * {@inheritdoc}
   */
  public function serviceDestroy() {
    $this->login(FALSE);

    try {
      $this->removeDirectory($this->settings['directory']);
    }
    catch (StorageException $e) {

    }
  }

  /**
   * Change to the directory that contains the file.
   *
   * @param string $filename
   *   The file name.
   *
   * @return string
   *   The directory path.
   */
  private function enterFileDirectory($filename) {
    $components = explode('/', $filename);
    $basename = array_pop($components);
    $path = '';

    foreach ($components as &$directory) {
      $exists = FALSE;
      $count = 0;

      do {
        $potential = (!$count) ? $directory : ($directory . '_' . $count++);

        try {
          try {
            $this->changeDirectory($potential);
          }
          catch (StorageException $e) {
            $this->createDirectory($potential);
            $this->changeDirectory($potential);
          }
        }
        catch (StorageException $e) {
          $count++;
          continue;
        }

        break;
      } while (TRUE);

      $directory = $potential;
    }

    $components[] = $basename;

    return implode('/', $components);
  }

  /**
   * {@inheritdoc}
   */
  public function serviceInstanceCreate(Storage $storage, $uri) {
    $this->login();
    $original_dir = ftp_pwd($this->ftpStream);

    $reference = $this->uniqueFilename($this->enterFileDirectory($storage->filename));

    $components = explode('/', $reference);

    $result = ftp_put($this->ftpStream, array_pop($components), $uri, FTP_BINARY);
    $this->changeDirectory($original_dir);

    if (!$result) {
      throw new StorageException();
    }

    if ($this->settings['mode']) {
      $this->setMode($reference, $this->settings['mode']);
    }

    return $reference;
  }

  /**
   * Lists all files in the directory.
   *
   * @param string $dir
   *   The directory.
   *
   * @return array
   *   An array of file names.
   */
  private function listItems($dir) {
    $files = [];

    $items = ftp_rawlist($this->ftpStream, $dir);

    foreach ($items as $item) {

      $mode = strtok($item, ' ');

      $i = 7;
      while ($i--) {
        strtok(' ');
      }

      $filename = strtok('');
      // Ignore files and directory names begining with a period.
      if ($filename[0] == '.') {
        continue;
      }

      switch ($mode[0]) {

        case 'd':
          foreach ($this->listItems($dir . '/' . $filename) as $subfilename) {
            $files[] = $filename . '/' . $subfilename;
          }
          break;

        case '-':
          $files[] = $filename;
          break;
      }
    }

    return $files;
  }

  /**
   * {@inheritdoc}
   */
  public function serviceListReferences() {
    $this->login();

    return $this->listItems('.');
  }

  /**
   * {@inheritdoc}
   */
  public function serviceInstanceExists($reference) {
    $this->login();

    return ftp_size($this->ftpStream, $reference) != -1;
  }

  /**
   * {@inheritdoc}
   */
  public function serviceInstanceDestroy($reference) {
    $this->login();

    ftp_delete($this->ftpStream, $reference);

    $dirs = explode('/', $reference);
    array_pop($dirs);

    while (count($dirs)) {

      try {
        $this->removeDirectory(implode('/', $dirs));
      }
      catch (StorageException $e) {
        break;
      }

      array_pop($dirs);
    }
  }

  /**
   * {@inheritdoc}
   */
  public function serviceInstanceGetUri($reference) {
    $this->login();

    $temp_uri = new StorageTempURI();
    $result = ftp_get($this->ftpStream, $temp_uri, $reference, FTP_BINARY);

    if (!$result) {
      throw new StorageException();
    }

    return $temp_uri;
  }

}
