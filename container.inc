<?php

/**
 * @file
 * Container class file for Storage API.
 */

abstract class StorageContainer {

  /**
   * Container ID number.
   * @var int
   */
  public $container_id;

  /**
   * Service ID number.
   * @var int
   */
  public $service_id;

  /**
   * Container name.
   * @var string
   */
  public $name;

  /**
   * Flag to determine access control is on or of for a container.
   * @var bool
   */
  public $access_control;

  /**
   * Flag to determine if the container is external or not.
   * @var bool
   */
  public $external;

  /**
   * Current status of the container.
   * @var string
   */
  public $status;

  /**
   * Array of settings for a container.
   * @var array
   */
  public $settings;

  public function __construct(array $fields) {

    foreach ($fields as $key => $value) {
      $this->$key = $value;
    }

    $this->settings += $this->serviceSettingsDefault();
  }

  public function service() {
    return storage_service_load($this->service_id);
  }

  /**
   * Count the number of storage instances for a the current container.
   *
   * @return int
   */
  public function count() {

    return db_select('storage_instance')
      ->condition('container_id', $this->container_id)
      ->isNotNull('file_id')
      ->countQuery()
      ->execute()
      ->fetchField();
  }

  /**
   * Return the file size of the entire container, summing all file sizes.
   *
   * @param bool $format
   * @return int|string
   */
  public function size($format = FALSE) {

    $query = db_select('storage_instance');
    $query->join('storage_file', NULL, 'storage_instance.file_id = storage_file.file_id');
    $query->addExpression('SUM(storage_file.size)', 'size');

    $size = (int) $query->condition('container_id', $this->container_id)
      ->execute()
      ->fetchField();

    return $format ? storage_format_byte_count($size) : $size;
  }

  /**
   * Return the status of the current container.
   *
   * @return string
   */
  public function status() {

    switch ($this->status) {
      case STORAGE_CONTAINER_STATUS_ACTIVE:
        return t('Active');
        break;

      case STORAGE_CONTAINER_STATUS_SUSPENDED:
        return t('Suspended');
        break;

      case STORAGE_CONTAINER_STATUS_DESTROY:
        return t('Being destroyed');
        break;
    }
  }

  /**
   * Creates an htaccess file in the container.
   * @throws \StorageException
   */
  public function htaccessCreate() {

    if (!function_exists('file_htaccess_lines')) {
      $msg = 'Unable to create .htaccess file before updating to latest Drupal Core';
      watchdog('storage', $msg, [], WATCHDOG_ERROR);
      throw new StorageException(t($msg));
    }
    $lines = file_htaccess_lines($this->access_control);
    if (!$this->access_control) {
      $lines .= "\n";
      $lines .= '<IfModule mod_headers.c>
      Header set Content-Disposition "attachment"
      </IfModule>';
    }

    $temp_uri = new StorageTempURI();
    file_put_contents($temp_uri, $lines);

    $storage = new Storage(['filename' => '.htaccess']);

    try {
      $this->serviceInstanceCreate($storage, $temp_uri);
    }
    catch (StorageException $e) {

    };
  }

  /**
   * Creates a storage container.
   *
   * @throws StorageException
   *   When it was not possible to create the container.
   */
  public function create($already_exists = FALSE) {

    if (!$already_exists) {
      try {
        $this->serviceCreate();
      }
      catch (StorageException $e) {
        $msg = 'Failed to create storage container @container.';
        watchdog('storage', $msg, ['@container' => $this->name()], WATCHDOG_ERROR);
        throw new StorageException(t($msg, ['@container' => $this->name()]));
      }
    }

    drupal_write_record('storage_container', $this);

    if ($this->servicePostCreate()) {
      $this->update();
    }

    if ($this->service()->htaccess) {
      $this->htaccessCreate();
    }

    $msg = 'Storage container !container has been created.';
    drupal_set_message(t($msg, ['!container' => $this->link()]));
    watchdog('storage', $msg, ['!container' => check_plain($this->name())], WATCHDOG_NOTICE, l(t('view'), $this->path()));
  }

  /**
   * Return the name of the service.
   *
   * @return string
   */
  public function name() {
    return $this->service()->name . ($this->name ? (' - ' . $this->name) : '');
  }

  /**
   * Returns the path to the admin UI for the current container.
   * @return string
   */
  public function path() {
    return 'admin/structure/storage/container/' . $this->container_id;
  }

  /**
   * Returns an HTML formatted link.
   *
   * @return mixed
   */
  public function link() {
    return l($this->name(), $this->path());
  }

  /**
   * Update the container information of the current container.
   */
  public function update() {
    drupal_write_record('storage_container', $this, 'container_id');
  }

  /**
   * Flush Serving Container.
   * @todo needs more investigation.
   */
  public function flushServingContainer() {

    $selector_subquery = db_select('storage_class_container');
    $selector_subquery->join('storage_selector', NULL, 'storage_class_container.class_id = storage_selector.class_id');

    $selector_subquery->fields('storage_selector', ['selector_id'])
      ->condition('storage_class_container.container_id', $this->container_id);

    db_update('storage')
      ->expression('serving_container', 'NULL')
      ->condition('selector_id', $selector_subquery, 'IN')
      ->execute();

    cache_clear_all();
  }

  /**
   *
   *
   * @param \Storage $storage
   * @param string $uri
   * @return mixed
   * @throws \StorageException
   */
  public function instanceCreate(Storage $storage, $uri) {

    if ($this->status == STORAGE_CONTAINER_STATUS_SUSPENDED || $this->external) {
      throw new StorageException();
    }

    $reference = $this->serviceInstanceCreate($storage, $uri);

    $instance = [
      'container_id' => $this->container_id,
      'file_id' => $storage->file_id,
      'reference' => $reference,
    ];

    drupal_write_record('storage_instance', $instance);

    $storage->flushServingContainer();

    $message = 'File instance created: ' . $storage->filename . '<br />';
    $message .= $this->logInfo();
    $message .= $storage->logInfo();

    watchdog('storage', $message, NULL);

    return $reference;
  }

  public function instanceCopy(Storage $storage, StorageContainerInterface $source_container) {

    if ($this->status == STORAGE_CONTAINER_STATUS_SUSPENDED || $this->external) {
      throw new StorageException();
    }

    $reference = $this->serviceInstanceCopy($storage, $source_container);

    $instance = [
      'container_id' => $this->container_id,
      'file_id' => $storage->file_id,
      'reference' => $reference,
    ];

    drupal_write_record('storage_instance', $instance);

    $storage->flushServingContainer();

    $message = 'File instance copied: ' . $storage->filename . '<br />';
    $message .= $this->logInfo();
    $message .= $storage->logInfo();

    watchdog('storage', $message, NULL);
  }

  public function tryInstanceCopy(Storage $storage) {

    // Find instances of the storage's file in other containers of the same type
    // that are in the storage's class.
    $query = db_select('storage_instance', NULL, ['fetch' => PDO::FETCH_ASSOC]);
    $query->join('storage_container', NULL, 'storage_instance.container_id = storage_container.container_id');
    $query->join('storage_class_container', NULL, 'storage_instance.container_id = storage_class_container.container_id');
    $query->join('storage_selector', NULL, 'storage_class_container.class_id = storage_selector.class_id');

    $result = $query->fields('storage_instance', ['container_id', 'reference'])
      ->condition('storage_instance.file_id', $storage->file_id)
      ->condition('storage_instance.container_id', $this->container_id, '<>')
      ->condition('storage_container.service_id', $this->service_id)
      ->condition('storage_selector.selector_id', $storage->selector_id)
      ->orderBy('storage_selector.migrating')
      ->orderBy('storage_class_container.weight', 'DESC')
      ->orderBy('storage_class_container.container_id')
      ->execute();

    foreach ($result as $row) {
      $source_container = storage_container_load($row['container_id']);
      $source_container->reference = $row['reference'];

      try {
        $this->instanceCopy($storage, $source_container);
      }
      catch (StorageException $e) {
        watchdog('storage', t('Instance copy failed.') . ' ' . $e, [], WATCHDOG_ERROR);
        continue;
      }

      return TRUE;
    }

    return FALSE;
  }

  /**
   * Ensure that an instance exists in the container.
   *
   * @param $storage
   *   The storage that there must be an instance of.
   *
   * @return bool
   *
   * @throws StorageException
   *   When it isn't possible to ensure that the instance exists.
   */
  public function ensureInstanceExists(Storage $storage) {
    // Do we have a file_id for the storage yet?
    if ($storage->file_id) {
      if ($this->instanceExists($storage)) {
        return;
      }
      $check = FALSE;
    }
    else {
      // We don't have a file_id yet, so we might check for an instance once we do.
      $check = TRUE;
    }

    // Is it possible to short circuit?
    if ($this->service()->copy && !$check) {
      if ($this->tryInstanceCopy($storage)) {
        return;
      }
    }

    $uri = $storage->getUri();
    $exists = FALSE;

    // If it's a new file, there is no point in checking for an instance.
    if ($check && !$storage->new_file) {
      $exists = $this->instanceExists($storage);
    }

    if (!$exists) {
      // Check to make sure that the source file actually exists.
      if (!file_exists($uri)) {
        watchdog(
          'storage_api',
          'File not found to be uploaded (!uri). Removing storage record',
          [
            '!uri' => $uri,
          ],
          WATCHDOG_ERROR
        );
        $storage->remove(TRUE);
        return FALSE;
      };
      $this->instanceCreate($storage, $uri);
    }
  }

  public function instanceExists(Storage $storage) {

    return db_select('storage_instance')
      ->fields('storage_instance', ['reference'])
      ->condition('file_id', $storage->file_id)
      ->condition('container_id', $this->container_id)
      ->execute()
      ->fetchField();
  }

  /**
   * Destroy an instance if it isn't required by any other storages.
   *
   * @param $storage
   *   The storage to have its instance destroyed.
   * @param $reference
   *   The reference of the instance to be destroyed.
   *
   * @throws StorageException
   *   When the instance should be destroyed, but it isn't possible.
   */
  public function instanceDestroyIfUnrequired(Storage $storage, $reference) {

    // Find other storages that use the same file.
    $storage_ids = db_select('storage')
      ->fields('storage', ['storage_id'])
      ->condition('storage.file_id', $storage->file_id)
      ->condition('storage.storage_id', $storage->storage_id, '<>')
      ->execute()
      ->fetchCol();

    if (count($storage_ids)) {

      // Are any of the other storages supposed to have an instance in this
      // container?
      $query = db_select('storage');
      $query->join('storage_selector', NULL, 'storage.selector_id = storage_selector.selector_id');
      $query->join('storage_class_container', NULL, 'storage_selector.class_id = storage_class_container.class_id');

      $count = $query->condition('storage.storage_id', $storage_ids, 'IN')
        ->condition('storage_class_container.container_id', $this->container_id)
        ->countQuery()
        ->execute()
        ->fetchField();

      if ($count > 0) {
        return;
      }

      // Do any of the other storages have this container as their initial
      // container?
      $count = db_select('storage')
        ->condition('storage_id', $storage_ids, 'IN')
        ->condition('initial_container_id', $this->container_id)
        ->countQuery()
        ->execute()
        ->fetchField();

      if ($count > 0) {
        return;
      }
    }

    $this->instanceDestroy($storage, $reference);
  }

  /**
   * Destroy an instance in the container
   *
   * @param $storage
   *   The storage to have its instance destroyed.
   * @param $reference
   *   The reference of the instance to be destroyed.
   *
   * @throws StorageException
   *   When it isn't possible to destroy the instance.
   */
  public function instanceDestroy(Storage $storage, $reference) {

    if ($this->status == STORAGE_CONTAINER_STATUS_SUSPENDED) {
      throw new StorageException();
    }

    // Silently fail to destroy an instance in an external container.
    if (!$this->external) {
      $this->serviceInstanceDestroy($reference);
    }

    db_delete('storage_instance')
      ->condition('file_id', $storage->file_id)
      ->condition('container_id', $this->container_id)
      ->execute();

    $message = 'File instance destroyed: ' . $storage->filename . '<br />';
    $message .= $this->logInfo();
    $message .= $storage->logInfo();

    watchdog('storage', $message, NULL);
  }

  public function instanceLink($reference) {

    if ($this->service()->serve) {
      return l($reference, $this->serviceInstanceServe($reference, FALSE));
    }
    else {
      return $reference;
    }
  }

  /**
   * Returns all classes configured for the current container.
   *
   * @return array
   */
  public function classes() {
    $classes = [];

    $result = db_select('storage_class_container', NULL, ['fetch' => PDO::FETCH_ASSOC])
      ->fields('storage_class_container', ['class_id'])
      ->condition('container_id', $this->container_id)
      ->orderBy('class_id')
      ->execute();

    foreach ($result as $class_info) {
      $classes[] = storage_class_load($class_info['class_id']);
    }

    return $classes;
  }

  /**
   * Delete the current container.
   */
  public function delete() {

    db_delete('storage_container')
      ->condition('container_id', $this->container_id)
      ->execute();

    db_delete('storage_class_container')
      ->condition('container_id', $this->container_id)
      ->execute();

    db_delete('storage_instance')
      ->condition('container_id', $this->container_id)
      ->execute();

    $this->flushServingContainer();
  }

  /**
   * Destroy the current container.
   */
  public function destroy() {

    try {
      $this->serviceDestroy();
      $this->delete();
    }
    catch (StorageException $e) {
      db_update('storage_container')
        ->fields(['status' => STORAGE_CONTAINER_STATUS_DESTROY])
        ->condition('container_id', $this->container_id)
        ->execute();
    }

    $message = 'Storage container <i>' . check_plain($this->name()) . '</i> has been destroyed.';
    drupal_set_message($message);
    watchdog('storage', $message, NULL);
  }

  /**
   * Determines if a reference exists in a container.
   *
   * The reference may either have a record in the database, or exist in the
   * container itself.
   *
   * @param $reference
   *   The reference to look for.
   * @return bool
   *   Whether the reference exists in the container.
   */
  public function referenceExists($reference) {

    $exists = (bool) db_select('storage_instance')
      ->condition('container_id', $this->container_id)
      ->condition('reference', $reference)
      ->countQuery()
      ->execute()
      ->fetchField();

    if ($exists) {
      return TRUE;
    }

    return $this->serviceInstanceExists($reference);
  }

  /**
   * Generates a unique filename for a container, based on a desired filename.
   *
   * @param $filename
   *   Desired filename.
   * @return string
   *   Unique filename.
   */
  public function uniqueFilename($filename) {

    if ($this->referenceExists($filename)) {

      $pos = strrpos($filename, '.');

      if ($pos !== FALSE) {
        $name = substr($filename, 0, $pos);
        $ext = substr($filename, $pos);
      }
      else {
        $name = $filename;
        $ext = '';
      }

      $count = 0;

      do {
        $filename = $name . '_' . $count++ . $ext;
      } while ($this->referenceExists($filename));
    }

    return $filename;
  }

  /**
   * Returns a formatted string for logging information about the current
   * container.
   *
   * @return string
   */
  public function logInfo() {

    $info = 'container: ' . check_plain($this->name()) . ', <br />';

    return $info;
  }

  /**
   * Default implementations of a service manifest.
   *
   * @return array
   */
  public function serviceManifest(array &$sandbox, &$finished) {

    if (!isset($sandbox['references'])) {
      $sandbox['references'] = $this->serviceListReferences();
    }

    $count = 100;
    $manifest = [];

    while ($count-- && ($reference = array_shift($sandbox['references']))) {

      try {
        $info = $this->serviceInstanceCheck($reference);
      }
      catch (StorageException $e) {
        continue;
      }

      $manifest[$reference] = $info;
    }

    $finished = (count($sandbox['references']) == 0);

    return $manifest;
  }

  public function serviceInstanceGetContents($reference) {
    // $uri can be a StorageTempURI - keep an instance of it here otherwise
    // file_get_contents() will destroy it too soon.
    $uri = $this->serviceInstanceGetUri($reference);
    $contents = file_get_contents($uri);

    if ($contents === FALSE) {
      throw new StorageException();
    }

    return $contents;
  }

  public function serviceInstanceGetUri($reference) {

    $temp_uri = new StorageTempURI();
    $success = file_put_contents($temp_uri, $this->serviceInstanceGetContents($reference));

    if (!$success) {
      throw new StorageException();
    }

    return $temp_uri;
  }

  /**
   * Get information about an instance and return it as an array.
   *
   * @param $reference
   *   The reference of the instance the be examined.
   *
   * @return array
   *
   * @throws StorageException
   *   When it isn't possible to get information about the instance.
   */
  public function serviceInstanceCheck($reference) {
    $uri = $this->serviceInstanceGetUri($reference);
    $stat = @stat($uri);

    if ($stat === FALSE) {
      throw new StorageException();
    }

    $info = [
      'uri' => $uri,
      'size' => $stat['size'],
      'whirlpool' => hash_file('whirlpool', $uri, TRUE),
    ];

    return $info;
  }

  public function serviceInstanceServe($reference, $https) {
    return file_create_url($this->settings['base_url'] . '/' . $reference);
  }

  public function serviceInstanceOutput($reference) {
    // $uri can be a StorageTempURI - keep an instance of it here otherwise
    // readfile() will destroy it too soon.
    $uri = $this->serviceInstanceGetUri($reference);
    readfile($uri);
  }

  /**
   * Optional implementation.
   *
   * @return array
   */
  public function serviceSettingsDefault() {
    return [];
  }

  /**
   * Optional implementation.
   */
  public function servicePostCreate() {

  }

  /**
   * Optional implementation.
   */
  public function serviceUpdate() {

  }

  /**
   * Optional implementation.
   */
  public function serviceMaintenance() {

  }

  /**
   * Optional implementation.
   */
  public function serviceListReferences() {
    throw new StorageException();
  }

  /**
   * Optional implementation.
   *
   * @param $storage
   * @param $source_container
   */
  public function serviceInstanceCopy(Storage $storage, StorageContainerInterface $source_container) {

  }

}

/**
 * Interface StorageContainerInterface
 *
 * All of the methods that must be included in a class for creating container
 * services.
 */
interface StorageContainerInterface {

  public function __construct(array $fields);

  public function service();

  public function count();

  public function size($format = FALSE);

  public function status();

  public function create($already_exists = FALSE);

  public function name();

  public function path();

  public function link();

  public function update();

  public function flushServingContainer();

  public function instanceCreate(Storage $storage, $uri);

  public function instanceCopy(Storage $storage, StorageContainerInterface $source_container);

  public function tryInstanceCopy(Storage $storage);

  public function ensureInstanceExists(Storage $storage);

  public function instanceExists(Storage $storage);

  public function instanceDestroyIfUnrequired(Storage $storage, $reference);

  public function instanceDestroy(Storage $storage, $reference);

  public function instanceLink($reference);

  public function classes();

  public function delete();

  public function destroy();

  public function referenceExists($reference);

  public function uniqueFilename($filename);

  public function logInfo();

  public function serviceSettingsForm($already_exists);

  public function serviceSettingsValidate($already_exists);

  /**
   * Perform additional tasks when a container service is created. This augments
   * the Storage core processes. This is useful for performing service specific
   * operations.
   *
   * @return mixed
   */
  public function serviceCreate();

  public function servicePostCreate();

  /**
   * Add additional data about a service beyond the standard information fields.
   * This information is used on the container view page. This does not override
   * the defaults but adds to the defaults.
   *
   * @return array
   */
  public function serviceInfo();

  public function serviceUpdate();

  public function serviceDestroy();

  public function serviceMaintenance();

  public function serviceListReferences();

  public function serviceManifest(array &$sandbox, &$finished);

  public function serviceInstanceCreate(Storage $storage, $uri);

  public function serviceInstanceCopy(Storage $storage, StorageContainerInterface $source_container);

  public function serviceInstanceExists($reference);

  public function serviceInstanceDestroy($reference);

  public function serviceInstanceGetContents($reference);

  public function serviceInstanceGetUri($reference);

  public function serviceInstanceCheck($reference);

  public function serviceInstanceServe($reference, $https);

  public function serviceInstanceOutput($reference);
}
