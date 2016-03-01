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
    if (module_exists('libraries') && function_exists('libraries_load')) {
      $library = libraries_load('google-api-php-client');
    }

    if (!isset($library) || $library['loaded'] == FALSE) {
      watchdog('storage_google', 'google-api-php-client not found', [], WATCHDOG_ERROR);

      if (user_access('administer storage')) {
        drupal_set_message(t('google-api-php-client not found. Check the <a href="!path">status report</a>.', ['!path' => url('admin/reports/status')]), 'error');
      }

      throw new StorageException();
    }

    $client_email = $this->settings['client_email'];
    $private_key = $this->settings['private_key_content'];
    $scopes = ['https://www.googleapis.com/auth/devstorage.full_control'];
    $credentials = new Google_Auth_AssertionCredentials(
      $client_email,
      $scopes,
      $private_key
    );

    $this->client = new Google_Client();
    $this->client->setAssertionCredentials($credentials);
    if ($this->client->getAuth()->isAccessTokenExpired()) {
      $this->client->getAuth()->refreshTokenWithAssertion();
    }

    $this->storageService = new Google_Service_Storage($this->client);
  }

  /**
   * {@inheritdoc}
   */
  public function serviceSettingsDefault() {
    return [
      'client_email' => '',
      'private_key' => '',
      'private_key_content' => '',
      'bucket' => '',
    ];
  }

  /**
   * {@inheritdoc}
   */
  public function serviceSettingsForm($already_exists) {
    $items['credentials_info'] = [
      '#type' => 'html_tag',
      '#tag' => 'p',
      '#value' => t('Credentials for your Service account (server-to-server)'),
    ];

    $items['client_email'] = [
      '#type' => 'textfield',
      '#title' => t('Email'),
      '#description' => t('The Email address for the Service account'),
      '#attributes' => [
        'placeholder' => 'name@project.iam.gserviceaccount.com',
      ],
      '#required' => TRUE,
      '#default_value' => $this->settings['client_email'],
    ];

    $items['private_key'] = [
      '#type' => 'managed_file',
      '#title' => t('Private key'),
      '#description' => t('Upload your private key from Google. P12 format is required.'),
      '#required' => TRUE,
      '#default_value' => $this->settings['private_key'],
      '#upload_validators' => [
        'file_validate_extensions' => ['p12'],
      ],
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
    // Store the content of the private key file.
    $file = file_load($this->settings['private_key']);
    $content = file_get_contents($file->uri);

    if ($content) {
      $this->settings['private_key_content'] = $content;
    }
    else {
      form_set_error('settings][private_key', t('Couldn\'t read the P12 file'));
    }

    if (form_get_errors()) {
      return;
    }

    try {
      $this->init();
    }
    catch (StorageException $e) {
      form_set_error('settings');
      return;
    }
  }

  /**
   * {@inheritdoc}
   */
  public function serviceInfo() {
    $info = [
      t('Email') => $this->settings['client_email'],
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

}
