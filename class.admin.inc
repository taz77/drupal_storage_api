<?php

/**
 * @file
 * Class adming UI functions for Storage API.
 */

/**
 * FAPI definition for the create class form.
 *
 * @ingroup forms
 * @see storage_create_class_form_submit()
 */
function storage_create_class_form(array $form, array &$form_state) {

  drupal_set_title(t('Create class'));

  $class = new StorageClass();

  $form = $class->formElements();

  $result = db_select('storage_container', NULL, ['fetch' => PDO::FETCH_ASSOC])
    ->fields('storage_container')
    ->orderBy('name')
    ->orderBy('container_id')
    ->execute();

  $containers = [];

  foreach ($result as $fields) {
    $container = storage_container_new($fields);
    $containers[$container->container_id] = check_plain($container->name());
  }

  $form['initial_container_id'] = [
    '#type' => 'select',
    '#title' => t('Initial container'),
    '#options' => $containers,
    '#description' => t('Files added to the class will initially only have an instance in this container.<br />If the initial container is not in the class, then files\' instances in it will be destroyed once they have been fully propagated.<br />When it is not possible to serve from a container in the class, this container will be used (while instances still exist).<br />Typically this is used to quickly store files locally on the server, providing a faster experience for the user. This is especially important for images rendered in-request.'),
  ];

  $form['submit'] = [
    '#type' => 'submit',
    '#value' => t('Create class'),
  ];

  return $form;
}

/**
 * Submit handler for the create class form.
 *
 * @see storage_create_class_form()
 */
function storage_create_class_form_submit(array $form, array &$form_state) {

  $class = new StorageClass($form_state['values']);
  $class->options['initial_container_id'] = $form_state['values']['initial_container_id'];
  $class->create();

  $form_state['redirect'] = $class->path() . '/add';
}

function _storage_class_selectors(StorageClass $class) {

  $result = db_select('storage_selector', NULL, ['fetch' => PDO::FETCH_ASSOC])
    ->fields('storage_selector')
    ->condition('storage_selector.class_id', $class->class_id)
    ->execute();

  foreach ($result as $selector_info) {
    unset($row);

    $row[] = $selector_info['selector_id'];
    $row[] = $selector_info['migrating'] ? t('Yes') : t('No');

    $rows[] = $row;
  }

  if (empty($rows)) {
    $rows[] = [
      [
        'data' => t('No selectors are using this class.'),
        'colspan' => 2,
        'class' => 'message',
      ],
    ];
  }

  $header = [t('Selector Id'), t('Migrating away')];

  $output = theme('table', [
    'header' => $header,
    'rows' => $rows,
    'attributes' => ['style' => 'width: auto;'],
  ]);

  return $output;
}

function storage_class(StorageClass $class) {
  drupal_add_js('misc/ajax.js');
  $info = [
    t('Files') => $class->count(),
    t('Size') => $class->size(TRUE),
    t('Serve by redirection') => $class->options['redirect'] ? t('Yes') : t('No'),
    t('Serve source') => $class->options['serve_source_uri'] ? t('Yes') : t('No'),
    t('Serve as data URI') => $class->options['data_uri'] ? t('Yes') : t('No'),
    t('Initial container') => (isset($class->initial_container) && $class->initial_container) ? $class->initial_container->link() : t('n/a'),
  ];

  $output = theme('storage_info_table', ['info' => $info]);

  $query = db_select('storage_class_container', NULL, ['fetch' => PDO::FETCH_ASSOC]);
  $query->join('storage_container', NULL, 'storage_class_container.container_id = storage_container.container_id');

  $query->fields('storage_container')
    ->fields('storage_class_container');

  $result = $query->condition('storage_class_container.class_id', $class->class_id)
    ->orderBy('storage_class_container.weight')
    ->orderBy('storage_container.container_id')
    ->execute();

  foreach ($result as $container_info) {
    unset($row);

    $container = storage_container_load($container_info['container_id']);
    $path = 'admin/structure/storage-ajax/' . $class->class_id . '/' . $container_info['container_id'] . '/stats';

    $row[] = $container->link();
    $row[] = $container_info['propagate'] ? t('Yes') : t('No');
    $row[] = $container_info['serving'] ? t('Yes') : t('No');
    $row[] = $container_info['remove'] ? t('Yes') : t('No');
    $row[] = l(t('Calculate'), $path . '/files/nojs', [
      'attributes' => [
        'class' => ['use-ajax'],
        'id' => ['target-' . $container_info['container_id'] . '-files'],
      ],
    ]);
    $row[] = l(t('Calculate'), $path . '/size/nojs', [
      'attributes' => [
        'class' => ['use-ajax'],
        'id' => ['target-' . $container_info['container_id'] . '-size'],
      ],
    ]);

    $rows[] = $row;
  }

  if (empty($rows)) {
    $rows[] = [
      [
        'data' => t('There are no containers in this class.'),
        'colspan' => 7,
        'class' => 'message',
      ],
    ];
  }

  $header = [
    t('Container'),
    t('Propagate'),
    t('Serving'),
    t('Remove'),
    t('Files'),
    t('Size'),
  ];

  $output .= theme('table', [
    'header' => $header,
    'rows' => $rows,
    'attributes' => ['style' => 'width: auto;'],
  ]);

  $output .= _storage_class_selectors($class);

  return $output;
}

/**
 * AJAX callback.
 */
function storage_class_ajax_stats($class_id, $container_id, $type, $ajax) {
  $commands = [];
  $query = db_select('storage_instance');
  $query->innerJoin('storage_file', NULL, 'storage_file.file_id = storage_instance.file_id');
  $query->innerJoin('storage', NULL, 'storage.file_id = storage_file.file_id');
  $query->innerJoin('storage_selector', NULL, 'storage.selector_id = storage_selector.selector_id');
  $query->condition('storage_instance.container_id', $container_id);
  $query->condition('storage_selector.class_id', $class_id);
  if ($type == 'files') {
    $query->addExpression('COUNT(storage.storage_id)', 'count');
  }
  elseif ($type == 'size') {
    $query->addExpression('SUM(storage_file.size)', 'size');
  }
  $result = $query->execute()->fetchField();
  if ($type == 'size') {
    $result = storage_format_byte_count($result);
  }
  if ($ajax == 'nojs') {
    return $result;
  }
  $target = '#target-' . $container_id . '-' . $type;
  $commands[] = ajax_command_replace($target, $result);
  ajax_deliver(['#type' => 'ajax', '#commands' => $commands]);
}

/**
 * FAPI definition for the class edit form.
 *
 * @ingroup forms
 * @see storage_class_edit_form_submit()
 */
function storage_class_edit_form(array $form, array &$form_state, StorageClass $class) {
  $form = $class->formElements();
  $form['#class'] = $class;

  $query = db_select('storage_class_container', NULL, ['fetch' => PDO::FETCH_ASSOC]);
  $query->join('storage_container', NULL, 'storage_class_container.container_id = storage_container.container_id');

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

  $form['#container_ids'] = [];
  $form['#tree'] = TRUE;

  foreach ($result as $container_info) {

    $container = storage_container_load($container_info['container_id']);

    $form['#container_ids'][] = $container->container_id;

    $form['names'][$container->container_id] = [
      '#markup' => $container->link(),
    ];

    $form['services'][$container->container_id] = [
      '#markup' => check_plain($container->service()->name),
    ];

    $form['weights'][$container->container_id] = [
      '#type' => 'weight',
      '#default_value' => $container_info['weight'],
      '#attributes' => ['class' => ['container-weight']],
    ];

    $form['propagates'][$container->container_id] = [
      '#type' => 'checkbox',
      '#default_value' => $container_info['propagate'],
    ];

    $form['servings'][$container->container_id] = [
      '#type' => 'checkbox',
      '#default_value' => $container_info['serving'],
    ];

    $form['removes'][$container->container_id] = [
      '#type' => 'checkbox',
      '#default_value' => $container_info['remove'],
    ];
  }

  $form['containers'] = [
    '#type' => 'fieldset',
    '#title' => t('Class containers'),
  ];

  $result = db_select('storage_container', NULL, ['fetch' => PDO::FETCH_ASSOC])
    ->fields('storage_container')
    ->orderBy('name')
    ->orderBy('container_id')
    ->execute();

  $containers = [];

  foreach ($result as $fields) {
    $container = storage_container_new($fields);
    $containers[$container->container_id] = check_plain($container->name());
  }

  $form['initial_container_id'] = [
    '#type' => 'select',
    '#title' => t('Initial container'),
    '#options' => $containers,
    '#default_value' => isset($class->options['initial_container_id']) ? $class->options['initial_container_id'] : FALSE,
    '#description' => t('Files added to the class will initially only have an instance in this container.<br />If the initial container is not in the class, then files\' instances in it will be destroyed once they have been fully propagated.<br />When it is not possible to serve from a container in the class, this container will be used (while instances still exist).<br />Typically this is used to quickly store files locally on the server, providing a faster experience for the user. This is especially important for images rendered in-request.'),
  ];

  $form['submit'] = [
    '#type' => 'submit',
    '#value' => t('Save'),
  ];

  return $form;
}

/**
 * Themes the class edit form.
 *
 * @ingroup themeable
 * @see storage_class_edit_form()
 */
function theme_storage_class_edit_form(array $elements) {

  $form = $elements['form'];

  $output = drupal_render($form['name']);
  $output .= drupal_render($form['options']);

  if (count($form['#container_ids'])) {

    $header = [
      t('Container'),
      t('Service'),
      t('Weight'),
      t('Propagate'),
      t('Serving'),
      t('Remove'),
    ];
    $rows = [];

    foreach ($form['#container_ids'] as $container_id) {
      unset($row);

      $row[] = drupal_render($form['names'][$container_id]);
      $row[] = drupal_render($form['services'][$container_id]);
      $row[] = drupal_render($form['weights'][$container_id]);

      $row[] = [
        'data' => drupal_render($form['propagates'][$container_id]),
        'align' => 'center',
      ];

      $row[] = [
        'data' => drupal_render($form['servings'][$container_id]),
        'align' => 'center',
      ];

      $row[] = [
        'data' => drupal_render($form['removes'][$container_id]),
        'align' => 'center',
      ];

      $rows[] = [
        'data' => $row,
        'class' => ['draggable'],
      ];
    }

    $form['containers']['table'] = [
      '#markup' => theme('table', [
        'header' => $header,
        'rows' => $rows,
        'attributes' => [
          'id' => 'class-containers-table',
          'style' => 'width: auto;',
        ],
      ]),
    ];

    $form['containers']['description'] = [
      '#markup' => "<div class=\"description\"><p>All files in the class are stored in all of the class's containers.</p><p>Containers at the top have the highest priority. They can be reordered using the drag-and-drop handles.</p><p>During cron, each file is propagated to the containers in priority order. When a file is fully propagated, instances of that file in containers that are not in the class are destroyed.</p><p>Files are retrieved from the highest priority container that has an instance of the file.</p><p>The same logic applies when serving a file, except that only serving containers are considered.</p><p>Suspended containers are not utilised for any purpose.</p><p>If a container is marked to be removed from the class, this will only occur once doing so will not cause any files to become unrecoverable, i.e. all files have an instance in another container.</p></div>",
    ];
  }

  $output .= drupal_render($form['containers']);

  drupal_add_tabledrag('class-containers-table', 'order', 'sibling', 'container-weight');

  $output .= drupal_render_children($form);
  return $output;
}

/**
 * Submit handler for the class edit form.
 *
 * @see storage_class_edit_form()
 */
function storage_class_edit_form_submit(array $form, array &$form_state) {
  $class = $form['#class'];

  $class->name = $form_state['values']['name'];
  $class->options = $form_state['values']['options'];
  $class->options['initial_container_id'] = $form_state['values']['initial_container_id'];
  $class->update();

  foreach ($form['#container_ids'] as $container_id) {

    $update = [
      'class_id' => $class->class_id,
      'container_id' => $container_id,
      'weight' => $form_state['values']['weights'][$container_id],
      'propagate' => $form_state['values']['propagates'][$container_id],
      'serving' => $form_state['values']['servings'][$container_id],
      'remove' => $form_state['values']['removes'][$container_id],
    ];

    drupal_write_record('storage_class_container', $update, [
      'class_id',
      'container_id',
    ]);
  }

  $selector_subquery = db_select('storage_selector')
    ->fields('storage_selector', ['selector_id'])
    ->condition('class_id', $class->class_id);

  db_update('storage')
    ->fields(['status' => STORAGE_STATUS_PROCESS_CRON])
    ->expression('serving_container', 'NULL')
    ->condition('selector_id', $selector_subquery, 'IN')
    ->execute();

  cache_clear_all();

  $message = 'Storage class %name has been updated.';
  $replacements = ['%name' => $class->name];

  drupal_set_message(t($message, $replacements));
  watchdog('storage', $message, $replacements, WATCHDOG_NOTICE, l(t('view'), $class->path()));

  $form_state['redirect'] = $class->path();
}

/**
 * FAPI definition for the class delete form.
 *
 * @ingroup forms
 * @see storage_class_delete_form_submit()
 */
function storage_class_delete_form(array $form, array &$form_state, StorageClass $class) {
  $form['#class'] = $class;

  if ($class->selectorCount() > 0) {

    $form[] = [
      '#prefix' => '<p>',
      '#suffix' => '</p>',
      '#markup' => t('It is not possible to delete this class at the moment, as it is in-use:') . '<br />' . _storage_class_selectors($class),
    ];
  }
  else {
    $form[] = [
      '#prefix' => '<p>',
      '#suffix' => '</p>',
      '#markup' => t('Are you sure you want to delete this storage class?'),
    ];

    $form['submit'] = [
      '#type' => 'submit',
      '#value' => t('Delete'),
    ];
  }

  return $form;
}

/**
 * Submit handler for the class delete form.
 *
 * @see storage_class_delete_form()
 */
function storage_class_delete_form_submit(array $form, array &$form_state) {
  $class = $form['#class'];

  if ($class->selectorCount() > 0) {
    $form_state['redirect'] = $class->path() . '/delete';
  }
  else {
    $class->delete();
    $form_state['redirect'] = 'admin/structure/storage';
  }
}

/**
 * FAPI definition for the class add container form.
 *
 * @ingroup forms
 * @see storage_class_add_container_form_submit()
 */
function storage_class_add_container_form(array $form, array &$form_state, StorageClass $class) {
  $form['#class'] = $class;


  $selector_subquery = db_select('storage_class_container')
    ->fields('storage_class_container', ['container_id'])
    ->condition('class_id', $class->class_id);

  $result = db_select('storage_container', NULL, ['fetch' => PDO::FETCH_ASSOC])
    ->fields('storage_container')
    ->condition('container_id', $selector_subquery, 'NOT IN')
    ->orderBy('name')
    ->orderBy('container_id')
    ->execute();

  $containers = [];

  foreach ($result as $fields) {
    $container = storage_container_new($fields);
    $containers[$container->container_id] = check_plain($container->name());
  }

  if (count($containers) == 0) {

    $form[] = [
      '#markup' => '<p>There are no containers left to add to the class.</p>',
    ];

    return $form;
  }

  $form['container_id'] = [
    '#title' => t('Container'),
    '#type' => 'select',
    '#required' => TRUE,
    '#options' => $containers,
    '#default_value' => 0,
  ];

  $form['submit'] = [
    '#type' => 'submit',
    '#value' => t('Add'),
  ];

  return $form;
}

/**
 * Submit handler for the class add container form.
 *
 * @see storage_class_add_container_form()
 */
function storage_class_add_container_form_submit(array $form, array &$form_state) {
  $class = $form['#class'];
  $container = storage_container_load($form_state['values']['container_id']);

  $class->addContainer($container);
  $form_state['redirect'] = $class->path();
}
