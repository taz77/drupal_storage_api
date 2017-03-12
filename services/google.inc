<?php

/**
 * @file
 * Google Cloud Storage class files for Storage API.
 */

/**
 * Provides a Google Cloud storage container.
 */
class StorageGoogle extends StorageContainer implements StorageContainerInterface {

  /**
   * @var
   */
  private $storageService;

  /**
   * @var
   */
  private $client;

  /**
   * Initialize the Google Cloud storage container.
   *
   * @throws \StorageException
   */
  private function init() {

    // There must be a private file system. Check for it's existence.
    $file_private_path = variable_get('file_private_path');
    if (empty($file_private_path)) {
      watchdog('storage_google', 'No private file system has been created', [], WATCHDOG_ERROR);
      if (user_access('administer storage')) {
        drupal_set_message(t('No private file system has been created. Please configure a private file system before proceeding.'), 'error');
      }
      throw new StorageException();
    }
    // If you want to use this service without the Libraries module or Composer
    // require_once '/path/to/google-api-php-client/vendor/autoload.php';

    // Check to see if class is loaded if not we attempt to load with libraries.
    if (!class_exists('Google_Client')) {
      if (module_exists('libraries') && function_exists('libraries_load')) {
        $library = libraries_load('google-api-php-client');
      }
    }

    // Check for the class again. If it is still not available we fail.
    if (!class_exists('Google_Client')) {
      watchdog('storage_google', 'google-api-php-client not found. Download the client on ' . l('Github', 'https://github.com/google/google-api-php-client#download-the-release', ['external' => TRUE]) . '. Then please consult the README on how to install the library.', [], WATCHDOG_ERROR);

      if (user_access('administer storage')) {
        drupal_set_message(t('google-api-php-client not found. Check the <a href="!path">status report</a>.', ['!path' => url('admin/reports/status')]), 'error');
      }

      throw new StorageException();
    }

    // Define the required scope(s)
    $scopes = implode(' ', array(
      Google_Service_Storage::DEVSTORAGE_READ_WRITE,
    ));

    $this->client = new Google_Client();
    $file = file_load($this->settings['service_json']);
    $this->client->setAuthConfig($file->uri);
    $this->client->setScopes($scopes);
    if ($this->client->isAccessTokenExpired()) {
      $this->client->refreshTokenWithAssertion();
    }


    $this->storageService = new Google_Service_Storage($this->client);
  }

  /**
   * {@inheritdoc}
   */
  public function serviceSettingsDefault() {
    return [
      'service_json' => '',
      'bucket' => '',
    ];
  }

  /**
   * {@inheritdoc}
   */
  public function serviceSettingsForm($already_exists) {
    // There must be a private file system. Check for it's existence.
    if (empty(variable_get('file_private_path'))) {
      $items['storage_error'] = [
        '#markup' => '<p>' . t('You have not specified a private file system.') . '</p>',
      ];

      watchdog('storage_google', 'No private file system has been created', [], WATCHDOG_ERROR);
      if (user_access('administer storage')) {
        drupal_set_message(t('No private file system has been created. Please') . ' ' . l('configure', 'admin/config/media/file-system') . ' ' . t('a private file system before proceeding.', ['@url']), 'error');
      }
      return $items;
    }

    // Check to see if class is loaded if not we attempt to load with libraries.
    if (!class_exists('Google_Client')) {
      if (module_exists('libraries') && function_exists('libraries_load')) {
        $library = libraries_load('google-api-php-client');
      }
    }

    // Check for the class again. If it is still not available we fail.
    if (!class_exists('Google_Client')) {
      $items['storage_error'] = [
        '#markup' => '<p>' . t('You have not loaded the Google API PHP Client library.') . '</p>',
      ];
      watchdog('storage_google', 'google-api-php-client not found. Download the client on ' . l('Github', 'https://github.com/google/google-api-php-client#download-the-release', ['external' => TRUE]) . '. Then please consult the README on how to install the library.', [], WATCHDOG_ERROR);

      if (user_access('administer storage')) {
        drupal_set_message(t('Google API PHP Client library not found. Check the <a href="!path">status report</a>.', ['!path' => url('admin/reports/status')]), 'error');
      }
      return $items;
    }


    $items['credentials_info'] = [
      '#markup' => '<p>' . t('Credentials for your Service account (server-to-server)') . '</p>',
    ];

    $items['service_json'] = [
      '#type' => 'managed_file',
      '#title' => t('Service JSON'),
      '#progress_message' => t('Please wait...'),
      '#progress_indicator' => 'bar',
      '#description' => t('Upload the Service JSON file that Google generated for your to download. WARNING: Do not upload a file containing spaces. This will cause problems.'),
      '#required' => TRUE,
      '#default_value' => $this->settings['service_json'],
      '#upload_validators' => [
        'file_validate_extensions' => ['json'],
      ],
      '#upload_location' => 'public://',
    ];

    $items['bucket'] = [
      '#type' => 'textfield',
      '#title' => t('Bucket'),
      '#description' => t('The machine name of the bucket at Google Cloud'),
      '#required' => TRUE,
      '#default_value' => $this->settings['bucket'],
    ];

    return $items;
  }

  /**
   * {@inheritdoc}
   */
  public function serviceSettingsValidate($already_exists) {
    // Load JSON file and make sure it is valid.
    $file = file_load($this->settings['service_json']);
    $content = file_get_contents($file->uri);
    $json = $this->storage_isJson($content);
    if ($json != 'JSON_ERROR_NONE') {
      form_set_error('settings][service_json', t('JSON is invalid. Check to make sure you uploaded the correct file and that it was not altered.'));
    }

    if (form_get_errors()) {
      return FALSE;
    }

    try {
      $this->init();
    }
    catch (StorageException $e) {
      form_set_error('settings');
      return FALSE;
    }
  }

  /**
   * {@inheritdoc}
   */
  public function serviceInfo() {
    $info = [
      t('Bucket') => $this->settings['bucket'],
    ];

    return $info;
  }

  /**
   * {@inheritdoc}
   */
  public function serviceCreate() {
    // Do nothing.
  }

  /**
   * {@inheritdoc}
   */
  public function serviceDestroy() {
    // Do nothing.
  }

  /**
   * {@inheritdoc}
   */
  public function serviceInstanceCreate(Storage $storage, $uri) {
    $this->init();
    $reference = $this->uniqueFilename($storage->filename);

    try {
      $params = [
        'name' => $reference,
        'data' => file_get_contents($uri),
        'uploadType' => 'media',
        'mimeType' => $storage->mimetype,
      ];

      // Set the file to public.
      $acl = new Google_Service_Storage_ObjectAccessControl();
      $acl->setEntity('allUsers');
      $acl->setRole('OWNER');

      $storage_object = new Google_Service_Storage_StorageObject();
      $storage_object->setName($reference);
      $storage_object->setAcl([$acl]);
      $this->storageService->objects->insert($this->settings['bucket'], $storage_object, $params);
    }
    catch (Exception $e) {
      throw new StorageException();
    }

    return $reference;
  }

  /**
   * {@inheritdoc}
   */
  public function serviceInstanceExists($reference) {
    $this->init();
    try {
      $this->storageService->objects->get($this->settings['bucket'], $reference);
    }
    catch (Exception $e) {
      return FALSE;
    }
    return TRUE;
  }

  /**
   * {@inheritdoc}
   */
  public function serviceInstanceDestroy($reference) {
    $this->init();
    try {
      $this->storageService->objects->delete($this->settings['bucket'], $reference);
    }
    catch (Exception $e) {
      throw new StorageException();
    }
  }

  /**
   * {@inheritdoc}
   */
  public function serviceInstanceGetContents($reference) {
    $uri = $this->serviceInstanceServe($reference);
    $contents = file_get_contents($uri);

    if ($contents === FALSE) {
      throw new StorageException();
    }

    return $contents;
  }

  /**
   * {@inheritdoc}
   */
  public function serviceInstanceServe($reference, $https) {
    $this->init();
    try {
      $result = $this->storageService->objects->get($this->settings['bucket'], $reference);
      return $result->mediaLink;
    }
    catch (Exception $e) {
      throw new StorageException();
    }
  }

  /**
   * Validate JSON strings.
   *
   * @param $string
   * @return bool
   */
  public function storage_isJson($string) {
    json_decode($string);
    return (json_last_error() == JSON_ERROR_NONE);
  }
}
