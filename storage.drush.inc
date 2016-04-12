<?php

/**
 * @file
 * Storage API drush commands.
 */

/**
 * Implements hook_drush_command().
 */
function storage_drush_command() {
  $items['storage-cron'] = [
    'description' => 'Performs the entire storage API cron.',
  ];

  $items['storage-sync-files'] = [
    'description' => 'Performs synchornizations of all files.',
  ];

  $items['storage-sync-migrations'] = [
    'description' => 'Performs synchornizations of migrations.',
  ];

  $items['storage-list-containers'] = [
    'description' => 'List all containers defined.',
  ];

  $items['storage-list-classes'] = [
    'description' => 'Lists all classes defined.',
  ];

  $items['storage-list-container-classes'] = [
    'description' => 'Lists all containers that have been placed in classes.',
  ];

  $items['storage-create-container'] = [
    'description' => 'Creates a storage container.',
    'arguments' => [
      'service_id' => 'The storage service id.',
      'name' => 'The human readable name.',
    ],
    'options' => [
      'access' => 'Defines that access control is enabled.',
      'exists' => 'Defines that the container itself already exists.',
    ],
    'allow-additional-options' => TRUE,
  ];

  $items['storage-modify-container'] = [
    'description' => 'Modifies a storage container',
    'arguments' => [
      'service_id' => 'The storage service id.',
      'name' => 'The human readable name.',
    ],
    'allow-additional-options' => TRUE,
  ];

  $items['storage-destroy-container'] = [
    'description' => 'Destroys a storage container.',
    'arguments' => [
      'service_id' => 'The storage service id.',
      'name' => 'The human readable name.',
    ],
    'options' => [
      'delete' => 'Indicates only the database records are meant to be deleted.',
    ],
  ];

  $items['storage-container-class'] = [
    'description' => 'Adds, removes or modifies a storage container from a class.',
    'arguments' => [
      'service_id' => 'The storage service id.',
      'name' => 'The human readable name.',
    ],
    'options' => [
      'class' => 'The class ID (numeric) the container is being updated against.',
      'command' => 'The command to perform regarding the container and the class, can be one of "add", "remove", "modify".',
      'propagate' => 'Changes the value of propagate.',
      'serve' => 'Changes the value of serving.',
      'weight' => 'Changes the value of weighting.',
    ],
  ];

  return $items;
}

/**
 * Implements hook_drush_help().
 */
function storage_drush_help($section) {
  switch ($section) {
    case 'drush:storage-create-container':
      $output = dt('Creates a container using one of the following container types and its arguments.') . "\n";
      $output .= _storage_drush_help_containers();
      return $output;

    case 'drush:storage-modify-container':
      $output = dt('Creates a container using one of the following container types and its arguments.') . "\n";
      $output .= _storage_drush_help_containers();
      return $output;

    case 'meta:storage:title':
      return dt('Storage API commands');

    case 'meta:storage:summary':
      return dt('Automates interaction with the storage containers and classes.');
  }
}

/**
 * Render the service information and the settings required.
 *
 * @return string
 *   The output string.
 */
function _storage_drush_help_containers() {
  drush_bootstrap_max();
  // Storage module needs to be enabled to list which services are on.
  if (!module_exists('storage')) {
    return '';
  }

  $output = "\nList of service IDs, and their options:\n";
  foreach (storage_service_info() as $service_id => $info) {
    $class = _storage_drush_container($service_id);
    $options = $class->serviceSettingsDefault();
    $output .= "  {$service_id}\n";
    foreach ($options as $key => $value) {
      $output .= "    --{$key}";
      if (!empty($value)) {
        $output .= "\t\tdefault: {$value}";
      }
      $output .= "\n";
    }
    $output .= "\n";
  }

  return $output;
}

/**
 * Get a container class.
 *
 * @param string $service_id
 *   A valid service identifier.
 *
 * @return \StorageContainerInterface
 *   The container class for the service.
 */
function _storage_drush_container($service_id) {
  $class = storage_container_class($service_id);
  return new $class(['service_id' => $service_id, 'settings' => []]);
}

/**
 * Run the storage cron.
 */
function drush_storage_cron() {
  module_load_include('inc', 'storage', 'cron');
  _storage_cron();
}

/**
 * Attempts to sync all files.
 */
function drush_storage_sync_files() {
  module_load_include('inc', 'storage', 'cron');
  _storage_check_files();
}

/**
 * Attempts to sync all migrations.
 */
function drush_storage_sync_migrations() {
  module_load_include('inc', 'storage', 'cron');
  _storage_check_migrations();
}

/**
 * List all containers defined.
 */
function drush_storage_list_containers() {
  $results = db_select('storage_container')
    ->fields('storage_container')
    ->orderBy('name')
    ->orderBy('container_id')
    ->execute();

  drush_print("ID\tService ID\tName");
  foreach ($results as $object) {
    drush_print("{$object->container_id}\t{$object->service_id}\t{$object->name}");
  }
}

/**
 * List all classes defined.
 */
function drush_storage_list_classes() {
  $results = db_select('storage_class')
    ->fields('storage_class')
    ->orderBy('storage_class.name')
    ->orderBy('storage_class.class_id')
    ->execute();

  drush_print("ID\tName");
  foreach ($results as $object) {
    drush_print("{$object->class_id}\t{$object->name}");
  }
}

/**
 * List all containers that have been placed in classes.
 */
function drush_storage_list_container_classes() {
  $query = db_select('storage_class_container');
  $query->join('storage_container', NULL, 'storage_class_container.container_id = storage_container.container_id');
  $query->join('storage_class', NULL, 'storage_class_container.class_id = storage_class.class_id');

  $results = $query->fields('storage_container', [
    'container_id',
    'name',
    'service_id',
  ])
    ->fields('storage_class_container')
    ->fields('storage_container', ['name'])
    ->fields('storage_class', ['name'])
    ->orderBy('storage_class_container.weight')
    ->orderBy('storage_container.container_id')
    ->execute();

  drush_print("Cont\tClass\tPropagate\tServe\tWeight");
  foreach ($results as $object) {
    drush_print("{$object->storage_container_name}\t{$object->storage_class_name}\t{$object->propagate}\t{$object->serving}\t{$object->weight}");
  }
}

/**
 * Load the service container information, checking for required settings if
 * necessary.
 *
 * @param string $service_id
 *   The service ID of the container.
 *
 * @param string $name
 *   The name given to the particular container.
 *
 * @param bool $required
 *   Flag indicating that settings are required.
 *
 * @return array|FALSE
 *   An array of the settings or FALSE.
 */
function _drush_storage_get_container($service_id, $name, $required = FALSE) {
  $services = storage_service_info();

  // Check the service id is valid.
  if (!isset($services[$service_id])) {
    drush_log(dt('A valid service needs to be given.'), 'error');
    return FALSE;
  }

  // Must always provide a name.
  if (empty($name)) {
    drush_log(dt('A valid name needs to be given.'), 'error');
    return FALSE;
  }

  // Get the settings.
  $settings = [];
  $class = _storage_drush_container($service_id);
  $options = $class->serviceSettingsDefault();
  $form = $class->serviceSettingsForm(TRUE);

  foreach ($options as $key => $value) {
    if ($val = drush_get_option($key)) {
      // Checkboxes get automatically set to on, unless they are set to NULL.
      if (!empty($form[$key]['#type']) && $form[$key]['#type'] === 'checkbox') {
        $settings[$key] = $val ? TRUE : NULL;
      }
      else {
        $settings[$key] = $val;
      }
    }
    elseif (!empty($value)) {
      // Checkboxes get automatically set to on, unless they are set to NULL.
      if (!empty($form[$key]['#type']) && $form[$key]['#type'] === 'checkbox') {
        $settings[$key] = $value ? TRUE : NULL;
      }
      else {
        $settings[$key] = $value;
      }
    }
    elseif ($required) {
      drush_log(dt('Missing the required setting %setting.', ['%setting' => $key]), 'error');
      return FALSE;
    }
  }

  return $settings;
}

/**
 * Load the storage container indicated by the service id and the name.
 *
 * @param string $service_id
 *   The service ID of the container.
 *
 * @param string $name
 *   The name the storage container is listed as.
 *
 * @return \StorageContainerInterface|bool
 *   The container or FALSE.
 */
function _drush_storage_load_container($service_id, $name) {
  // Locate the appropriate container.
  $results = db_select('storage_container', 's')
    ->fields('s', ['container_id'])
    ->condition('service_id', $service_id, '=')
    ->condition('name', $name, '=')
    ->execute()
    ->fetchCol();

  if (empty($results)) {
    drush_log(t('Unable to locate %container with service ID %service.', [
      '%container',
      $name,
      '%service' => $service_id,
    ]), 'error');
    return FALSE;
  }

  // Load the container.
  return storage_container_load(array_shift($results));
}

/**
 * Creates a storage container.
 *
 * @param string $service_id
 *   The service ID of the container.
 *
 * @param string $name
 *   The name the storage container is listed as.
 */
function drush_storage_create_container($service_id, $name) {
  // Ensure the settings and the service id and name are somewhat valid.
  if (($settings = _drush_storage_get_container($service_id, $name, TRUE)) === FALSE) {
    return;
  }

  // Setup the $form_state to match what would be passed normally.
  $form_state['storage'] = [
    'service_id' => $service_id,
    'name' => $name,
    'access control' => drush_get_option('access'),
    'already exists' => drush_get_option('exists'),
    'settings' => [],
  ];
  $form_state['values']['step'] = 1;
  $form_state['values']['settings'] = $settings;

  // Call the submit function for this behaviour.
  module_load_include('inc', 'storage', 'container.admin');
  drupal_form_submit('storage_create_container_form', $form_state);
}

/**
 * Modifies a storage container.
 *
 * @param string $service_id
 *   The service ID of the container.
 *
 * @param string $name
 *   The name the storage container is listed as.
 */
function drush_storage_manage_container($service_id, $name) {
  // Ensure the settings and the service id and name are somewhat valid.
  if (($settings = _drush_storage_get_container($service_id, $name, FALSE)) === FALSE) {
    return;
  }

  // Ensure the container exists to be modified.
  if (!($container = _drush_storage_load_container($service_id, $name))) {
    return;
  }

  // Programmatically submitting the storage container form.
  $form_values['values'] = [
    'name' => $container->name,
    'external' => $container->external,
    'settings' => $settings,
  ];

  // Add the container argument for the form.
  $form_state['build_info']['args'] = [&$container];

  // Call the submit function for this behaviour.
  module_load_include('inc', 'storage', 'container.admin');
  drupal_form_submit('storage_container_edit_form', $form_values);
}

/**
 * Destroy a storage container.
 *
 * @param string $service_id
 *   The service ID of the container.
 *
 * @param string $name
 *   The name the storage container is listed as.
 */
function drush_storage_destroy_container($service_id, $name) {
  // Ensure the settings and the service id and name are somewhat valid.
  if (($settings = _drush_storage_get_container($service_id, $name, TRUE)) === FALSE) {
    return;
  }

  // Ensure the container exists to be modified.
  if (!($container = _drush_storage_load_container($service_id, $name))) {
    return;
  }

  // Perform a database delete as opposed to destroying the container.
  if (!empty($container->external) || !drush_get_option('delete')) {
    $container->delete();
  }
  else {
    $container->destroy();
  }
}

/**
 * Add, modify or remove a container class configuration.
 *
 * @param string $service_id
 *   The service ID for the container.
 *
 * @param string $name
 *   The human readable name of the container.
 */
function drush_storage_container_class($service_id, $name) {
  if (($settings = _drush_storage_get_container($service_id, $name)) === FALSE) {
    return;
  }

  // Ensure valid operation against the container and class.
  $command = drush_get_option('command');
  if (!in_array($command, ['add', 'remove', 'modify'])) {
    drush_log(dt('Unknown command specified.'), 'error');
    return;
  }

  // Ensure the container exists to be modified.
  if (!($container = _drush_storage_load_container($service_id, $name))) {
    return;
  }

  // Search for matching class ID.
  $results = db_select('storage_class')
    ->fields('storage_class', ['class_id'])
    ->condition('class_id', drush_get_option('class'))
    ->execute()
    ->fetchAssoc();

  // Check for valid class.
  if (empty($results)) {
    drush_log(dt('No valid class ID has been specified.'), 'error');
    return;
  }

  // Get the class, as loaded from the database.
  $class = storage_class_load($results['class_id']);

  // At this point we can add the container to the class, as this should update
  // the database to provide the default behaviours.
  if ($command === 'add') {
    $class->addContainer($container);
  }

  // Locate all the class containers that match the given requirements.
  $query = db_select('storage_class_container', NULL, ['fetch' => PDO::FETCH_ASSOC]);
  $query->join('storage_container', NULL, 'storage_class_container.container_id = storage_container.container_id');

  // Gets the storage_containers that match the class and the service ID.
  $result = $query->fields('storage_container', [
    'container_id',
    'name',
    'service_id',
  ])
    ->fields('storage_class_container')
    ->condition('storage_class_container.class_id', $class->class_id)
    ->orderBy('storage_class_container.weight')
    ->orderBy('storage_container.container_id')
    ->execute();

  // Setup the values for the class to be processed for the submit.
  $form_state['values'] = [
    'name' => $class->name,
    'initial_container_id' => $class->options['initial_container_id'],
  ];
  // Checkboxes require either some value or NULL to correctly set their value.
  foreach ($class->options as $key => $value) {
    $form_state['values']['options'][$key] = $value ? $value : NULL;
  }

  // Generate the appropriate containers.
  foreach ($result as $item) {
    $id = $item['container_id'];
    $form_state['values']['weights'][$id] = $item['weight'] ? $item['weight'] : NULL;
    $form_state['values']['propagates'][$id] = $item['propagate'] ? $item['propagate'] : NULL;
    $form_state['values']['servings'][$id] = $item['serving'] ? $item['serving'] : NULL;
    $form_state['values']['removes'][$id] = $item['remove'] ? $item['remove'] : NULL;
  }

  // Setup map of key to form_values key.
  $map = [
    'serve' => 'servings',
    'propagate' => 'propagates',
    'weight' => 'weights',
  ];

  // Map settings into the form values.
  foreach ($map as $key => $map_key) {
    $value = drush_get_option($key);
    if (!is_null($value)) {
      $form_state['values'][$map_key][$container->container_id] = $value ? TRUE : NULL;
    }
  }

  // Remove the particular item.
  if ($command === 'remove') {
    $form_state['values']['removes'][$container->container_id] = TRUE;
  }

  // Add the class argument for the form.
  $form_state['build_info']['args'] = [&$class];

  // Call the submit function for this behaviour.
  module_load_include('inc', 'storage', 'class.admin');
  drupal_form_submit('storage_class_edit_form', $form_state);
}
