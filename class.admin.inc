<?php

// Copyright 2008-2011 Jonathan Brown


function _storage_classes_query() {

  $query = db_select('storage_class', NULL, array('fetch' => PDO::FETCH_ASSOC));
  $query->leftJoin('storage_selector', NULL, 'storage_class.class_id = storage_selector.class_id');
  $query->leftJoin('storage', NULL, 'storage_selector.selector_id = storage.selector_id');
  $query->leftJoin('storage_file', NULL, 'storage.file_id = storage_file.file_id');
  
  $query->fields('storage_class')
    ->groupBy('storage_class.class_id');
    
  $query->addExpression('COUNT(storage.storage_id)', 'files');
  $query->addExpression('SUM(storage_file.size)', 'size');
  
  return $query;
}


function storage_classes_list() {

  drupal_set_title(t('Storage classes'));

  $result = _storage_classes_query()
    ->orderBy('storage_class.name')
    ->orderBy('storage_class.class_id')
    ->execute();

  $header = array(
    t('Name'), t('Files'), t('Size')
  );

  foreach ($result as $class) {
    unset($row);
    
    $row[] = l($class['name'], 'admin/structure/storage/classes/' . $class['class_id']);
    $row[] = $class['files'];
    $row[] = storage_format_size((int) $class['size']);
    
    $rows[] = $row;
  }

  if (empty($rows)) {
    $rows[] = array(array('data' => t('No classes configured.'), 'colspan' => 3, 'class' => 'message'));
  }

  return theme('table', array(
    'header' => $header, 
    'rows' => $rows,
    'attributes' => array('style' => 'width: auto;'),
  ));
}


/**
 * FAPI definition for the create class form.
 *
 * @ingroup forms
 * @see storage_create_class_form_submit()
 */
function storage_create_class_form($form, &$form_state) {

  drupal_set_title(t('Create class'));
  
  $class = new StorageClass(); 
  
  $form = $class->formElements();

  $result = db_select('storage_container', NULL, array('fetch' => PDO::FETCH_ASSOC))
    ->fields('storage_container')
    ->orderBy('name')
    ->orderBy('container_id')
    ->execute();
    
  $containers = array();
 
  foreach ($result as $container) {
    $containers[$container['container_id']] = $container['name'];
  }

  $form['initial_container_id'] = array(
    '#type' => 'select',
    '#title' => t('Initial container'),
    '#options' => $containers,
    '#description' => t('Files added to the class will initially only have an instance in this container.<br />If the initial container is not in the class, then files\' instances in it will be destroyed once they have been fully propagated.<br />When it is not possible to serve from a container in the class, this container will be used (while instances still exist).<br />Typically this is used to quickly store files locally on the server, providing a faster experience for the user. This is especially important for images rendered in-request.'),
  );

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Create class')
  );
  
  return $form;
}


/**
 * Submit handler for the create class form.
 *
 * @see storage_create_class_form()
 */
function storage_create_class_form_submit($form, &$form_state) {

  $class = new StorageClass($form_state['values']);
  $class->options['initial_container_id'] = $form_state['values']['initial_container_id'];
  $class->create();
  
  $form_state['redirect'] = $class->path() . '/add';
}


function _storage_class_selectors(StorageClass $class) {

  $result = db_select('storage_selector', NULL, array('fetch' => PDO::FETCH_ASSOC))
    ->fields('storage_selector')
    ->condition('storage_selector.class_id', $class->class_id)
    ->execute();

  foreach ($result as $selector_info) {
    unset($row);

    $row[] = $selector_info['selector_id'];
    $row[] = $selector_info['migrating'] ? t('Yes') : t('No');

    $rows[] = $row;
  }

  if(empty($rows)) {
    $rows[] = array(array('data' => t('No selectors are using this class.'), 'colspan' => 2, 'class' => 'message'));
  }

  $header = array(t('Selector Id'), t('Migrating away'));

  $output = theme('table', array(
    'header' => $header,
    'rows' => $rows,
    'attributes' => array('style' => 'width: auto;'),
  ));

  return $output;
}


function storage_class(StorageClass $class) {

  $class_info = _storage_classes_query()
    ->havingCondition('storage_class.class_id', $class->class_id)
    ->execute()
    ->fetchAssoc();
  
  $info = array(
    t('Files') => $class_info['files'],
    t('Size') => storage_format_size((int) $class_info['size']),
    t('Redirection') => (isset($class->options['redirect']) && $class->options['redirect']) ? t('Yes') : t('No'),
    t('Serve source') => (isset($class->options['serve_source_url']) && $class->options['serve_source_url']) ? t('Yes') : t('No'),
    t('Initial container') => (isset($class->initial_container) && $class->initial_container) ? $class->initial_container->link() : t('n/a'),
  );
  
  $output = theme('storage_info_table', array('info' => $info));

  $subquery = db_select('storage_selector');
  $subquery->join('storage', NULL, 'storage_selector.selector_id = storage.selector_id');
  $subquery->join('storage_file', NULL, 'storage.file_id = storage_file.file_id');
  $subquery->join('storage_instance', NULL, 'storage.file_id = storage_instance.file_id');
  
  $subquery->fields('storage_instance', array('container_id'));
  $subquery->addExpression('COUNT(storage.storage_id)', 'count');
  $subquery->addExpression('SUM(storage_file.size)', 'size');
    
  $subquery->condition('storage_selector.class_id', $class->class_id)
    ->groupBy('storage_instance.container_id');
    
    
  $query = db_select('storage_class_container', NULL, array('fetch' => PDO::FETCH_ASSOC));
  $query->join('storage_container', NULL, 'storage_class_container.container_id = storage_container.container_id');
  $query->leftJoin($subquery, 'files', 'storage_container.container_id = files.container_id');

  $query->fields('storage_container')
    ->fields('storage_class_container');
    
  $query->addField('files', 'count', 'count');
  $query->addField('files', 'size', 'size');

  $result = $query->condition('storage_class_container.class_id', $class->class_id)
    ->orderBy('storage_class_container.weight')
    ->orderBy('storage_container.container_id')
    ->execute();

  foreach ($result as $container_info) {
    unset($row);
    
    $container = storage_container_load($container_info['container_id']);
    
    $row[] = $container->link();
    $row[] = $container->service()->name();
    $row[] = $container_info['serving'] ? t('Yes') : t('No');
    $row[] = $container_info['remove'] ? t('Yes') : t('No');
    $row[] = (int)$container_info['count'];
    $row[] = storage_format_size((int) $container_info['size']);
    
    $rows[] = $row;
  }

  if(empty($rows)) {
    $rows[] = array(array('data' => t('There are no containers in this class.'), 'colspan' => 5, 'class' => 'message'));
  }

  $header = array(t('Container'), t('Service'), t('Serving'), t('Remove'), t('Files'), t('Size'));
  
  $output .= theme('table', array(
    'header' => $header, 
    'rows' => $rows,
    'attributes' => array('style' => 'width: auto;'),
  ));

  $output .= _storage_class_selectors($class);

  return $output;
}


/**
 * FAPI definition for the class edit form.
 *
 * @ingroup forms
 * @see storage_class_edit_form_submit()
 */
function storage_class_edit_form($form, &$form_state, StorageClass $class) {

  $form = $class->formElements();
  $form['#class'] = $class;
  
  $query = db_select('storage_class_container', NULL, array('fetch' => PDO::FETCH_ASSOC));
  $query->join('storage_container', NULL, 'storage_class_container.container_id = storage_container.container_id');

  $result = $query->fields('storage_container', array('container_id', 'name', 'service_id'))
    ->fields('storage_class_container')
    ->condition('storage_class_container.class_id', $class->class_id)
    ->orderBy('storage_class_container.weight')
    ->orderBy('storage_container.container_id')
    ->execute();

  $form['#container_ids'] = array();
  $form['#tree'] = TRUE;
  
  foreach ($result as $container_info) {
  
    $container = storage_container_load($container_info['container_id']);
  
    $form['#container_ids'][] = $container->container_id;

    $form['names'][$container->container_id] = array(
      '#markup' => $container->link(),
    );

    $form['services'][$container->container_id] = array(
      '#markup' => $container->service()->name(),
    );

    $form['weights'][$container->container_id] = array(
      '#type' => 'weight', 
      '#default_value' => $container_info['weight'],
      '#attributes' => array('class' => array('container-weight')),
    );

    $form['servings'][$container->container_id] = array(
      '#type' => 'checkbox',
      '#default_value' => $container_info['serving'],
    );

    $form['removes'][$container->container_id] = array(
      '#type' => 'checkbox',
      '#default_value' => $container_info['remove'],
    );
  }

  $form['containers'] = array(
    '#type' => 'fieldset',
    '#title' => t('Class containers'),
    '#description' => t('<p>Broadly speaking, containers at the top should be nearest the client; those at the bottom should be nearest the server.</p><p>During cron, files are propagated to all the containers in their class (starting at the top). Once this is complete, any instances of files that are not in these containers will be destroyed.</p><p>Files are served from the container nearest the top that has an instance of the file and is capable of serving - it must not be suspended and it must also be configured to be serving within the class.</p>'),
  );

  $result = db_select('storage_container', NULL, array('fetch' => PDO::FETCH_ASSOC))
    ->fields('storage_container')
    ->orderBy('name')
    ->orderBy('container_id')
    ->execute();
    
  $containers = array();
 
  foreach ($result as $container) {
    $containers[$container['container_id']] = $container['name'];
  }

  $form['initial_container_id'] = array(
    '#type' => 'select',
    '#title' => t('Initial container'),
    '#options' => $containers,
    '#default_value' => isset($class->options['initial_container_id']) ? $class->options['initial_container_id'] : FALSE,
    '#description' => t('Files added to the class will initially only have an instance in this container.<br />If the initial container is not in the class, then files\' instances in it will be destroyed once they have been fully propagated.<br />When it is not possible to serve from a container in the class, this container will be used (while instances still exist).<br />Typically this is used to quickly store files locally on the server, providing a faster experience for the user. This is especially important for images rendered in-request.'),
  );

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Save')
  );

  return $form;
}


/**
 * Themes the class edit form.
 *
 * @ingroup themeable
 * @see storage_class_edit_form()
 */
function theme_storage_class_edit_form($elements) {
  
  $form = $elements['form'];

  $output = drupal_render($form['name']);
  $output .= drupal_render($form['options']);

  if (count($form['#container_ids'])) {
  
    $header = array(t('Container'), t('Service'), t('Weight'), t('Serving'), t('Remove'));
    $rows = array();

    foreach ($form['#container_ids'] as $container_id) {
      unset($row);
      
      $row[] = drupal_render($form['names'][$container_id]);
      $row[] = drupal_render($form['services'][$container_id]);
      $row[] = drupal_render($form['weights'][$container_id]);

      $row[] = array(
        'data' => drupal_render($form['servings'][$container_id]),
        'align' => 'center',
      );

      $row[] = array(
        'data' => drupal_render($form['removes'][$container_id]),
        'align' => 'center',
      );

      $rows[] = array(
        'data' => $row,
        'class' => array('draggable'),
      );
    }

    $form['containers']['table'] = array(
      '#markup' => theme('table', array(
        'header' => $header, 
        'rows' => $rows, 
        'attributes' => array('id' => 'class-containers-table', 'style' => 'width: auto;')
    )));
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
function storage_class_edit_form_submit($form, &$form_state) {
  $class = $form['#class'];

  $class->name = $form_state['values']['name'];
  $class->options = $form_state['values']['options'];
  $class->options['initial_container_id'] = $form_state['values']['initial_container_id'];
  $class->update();

  foreach ($form['#container_ids'] as $container_id) {

    $update = array(
      'class_id' => $class->class_id,
      'container_id' => $container_id,
      'weight' => $form_state['values']['weights'][$container_id],
      'serving' => $form_state['values']['servings'][$container_id],
      'remove' => $form_state['values']['removes'][$container_id],
    );

    drupal_write_record('storage_class_container', $update, array('class_id', 'container_id'));
  }
  
  $selector_subquery = db_select('storage_selector')
    ->fields('storage_selector', array('selector_id'))
    ->condition('class_id', $class->class_id);

  db_update('storage')
    ->expression('serving_container', 'NULL')
    ->condition('selector_id', $selector_subquery, 'IN')
    ->execute();
  
  cache_clear_all();
  
  $message = 'Storage class <i>' . $class->name() . '</i> has been updated.';

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $class->path()));

  $form_state['redirect'] = $class->path();
}


/**
 * FAPI definition for the class delete form.
 *
 * @ingroup forms
 * @see storage_class_delete_form_submit()
 */
function storage_class_delete_form($form, &$form_state, StorageClass $class) {
  $form['#class'] = $class;

  if ($class->selectorCount() > 0) {

    $form[] = array(
      '#markup' => t("<p>It is not possible to delete this class at the moment, as it is in-use:</p>") .
        _storage_class_selectors($class),
    );
  }
  else {
    $form[] = array(
      '#markup' => t("<p>Are you sure you want to delete this storage class?</p>")
    );

    $form['submit'] = array(
      '#type' => 'submit',
      '#value' => t('Delete')
    );
  }

  return $form;
}


/**
 * Submit handler for the class delete form.
 *
 * @see storage_class_delete_form()
 */
function storage_class_delete_form_submit($form, &$form_state) {
  $class = $form['#class'];

  if ($class->selectorCount() > 0) {
    $form_state['redirect'] = $class->path() . '/delete';
  }
  else {
    $class->delete();
    $form_state['redirect'] = 'admin/structure/storage/classes';
  }
}


/**
 * FAPI definition for the class add container form.
 *
 * @ingroup forms
 * @see storage_class_add_container_form_submit()
 */
function storage_class_add_container_form($form, &$form_state, StorageClass $class) {
  $form['#class'] = $class;
  

  $selector_subquery = db_select('storage_class_container')
    ->fields('storage_class_container', array('container_id'))
    ->condition('class_id', $class->class_id);

  $result = db_select('storage_container', NULL, array('fetch' => PDO::FETCH_ASSOC))
    ->fields('storage_container')
    ->condition('container_id', $selector_subquery, 'NOT IN')
    ->orderBy('name')
    ->orderBy('container_id')
    ->execute();
    
  $containers = array();
 
  foreach ($result as $container) {
    $containers[$container['container_id']] = $container['name'];
  }
  
  if(count($containers) == 0) {
  
    $form[] = array(
      '#markup' => '<p>There are no containers left to add to the class.</p>'
    );
    
    return $form;
  }

  $form['container_id'] = array(
    '#title' => t('Container'),
    '#type' => 'select',
    '#required' => TRUE,
    '#options' => $containers,
    '#default_value' => 0,
  );
  
  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Add')
  );

  return $form;
}


/**
 * Submit handler for the class add container form.
 *
 * @see storage_class_add_container_form()
 */
function storage_class_add_container_form_submit($form, &$form_state) {
  $class = $form['#class'];
  $container = storage_container_load($form_state['values']['container_id']);
  
  $class->addContainer($container);
  $form_state['redirect'] = $class->path();
}

