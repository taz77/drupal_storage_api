<?php
// $Id$

// Copyright 2008-2009 Jonathan Brown


function _storage_api_trash_icon() {

  return '<img src="' . base_path() . drupal_get_path('module', 'storage_api') . '/trash.png' . 
    '" style="vertical-align: middle;" />';
}


function _storage_api_class_name($class_id) {

  return db_select('storage_class')
    ->fields('storage_class', array('name'))
    ->condition('class_id', $class_id)
    ->excute()
    ->fetchField();
}


function _storage_api_classes_query() {

  $query = db_select('storage_class', NULL, array('fetch' => PDO::FETCH_ASSOC));
  $query->join('storage_selector', NULL, 'storage_class.class_id = storage_selector.class_id');
  $query->join('storage_file', NULL, 'storage_selector.selector_id = storage_file.selector_id');
  $query->join('storage_object', NULL, 'storage_file.object_id = storage_object.object_id');
  
  $query->fields('storage_class')
    ->groupBy('storage_class.class_id');
    
  $query->addExpression('COUNT(storage_file.file_id)', 'files');
  $query->addExpression('SUM(storage_object.size)', 'size');
  
  return $query;
}


function storage_api_classes_list() {

  drupal_set_title(t('Storage classes'), PASS_THROUGH);

  $result = _storage_api_classes_query()
    ->orderBy('storage_class.name')
    ->orderBy('storage_class.class_id')
    ->execute();

  $header = array(
    t('Name'), t('Files'), t('Size')
  );

  foreach($result as $class) {
    unset($row);
    
    $row[] = l($class['name'], 'admin/build/storage/classes/' . $class['class_id']);
    $row[] = $class['files'];
    $row[] = storage_api_format_size($class['size']);
    
    $rows[] = $row;
  }

  if(empty($rows)) {
    $rows[] = array(array('data' => t('No classes configured.'), 'colspan' => 3, 'class' => 'message'));
  }

  return theme('table', array(
    'header' => $header, 
    'rows' => $rows,
    'attributes' => array('style' => 'width: auto;'),
  ));
}


function _storage_api_class_form_elements($class = NULL) {

  $form['name'] = array(
    '#title' => t('Name'),
    '#type' => 'textfield',
    '#required' => TRUE,
    '#default_value' => $class['name']
  );
  
  $form['options'] = array(
    '#tree' => TRUE,
    'access_control' => array(
      '#title' => t('Enforce access control'),
      '#type' => 'checkbox',
      '#default_value' => $class['options']['access_control']
    ),
    'redirect' => array(
      '#title' => t('Serve by redirection'),
      '#type' => 'checkbox',
      '#default_value' => $class['options']['redirect']
    ),
    'serve_source_url' => array(
      '#title' => t('Serve source url'),
      '#type' => 'checkbox',
      '#default_value' => $class['options']['serve_source_url'],
      '#description' => t('If no instances are available, serve the source url.')
    )
  );
  
  return $form;
}  


/**
 * FAPI definition for the create class form.
 *
 * @ingroup forms
 * @see storage_api_create_class_form_submit()
 */
function storage_api_create_class_form($form_state) {

  drupal_set_title(t('Storage classes'), PASS_THROUGH);
  
  $form = _storage_api_class_form_elements(); 

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Create class')
  );
  
  return $form;
}


/**
 * Submit handler for the create class form.
 *
 * @see storage_api_create_class_form()
 */
function storage_api_create_class_form_submit($form, &$form_state) {

  $class = array(
    'name' => $form_state['values']['name'],
    'options' => $form_state['values']['options'],
  );
  
  drupal_write_record('storage_class', $class);

  $message = 'Storage class <i>' . $form_state['values']['name'] . '</i> has been created.';
  $path = 'admin/build/storage/classes/' . $class['class_id'];

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $path));

  $form_state['redirect'] = $path . '/add';
}


function storage_api_class($class) {

  drupal_set_title(t('Storage class: ') . $class['name'], PASS_THROUGH);
  
  $class_info = _storage_api_classes_query()
    ->havingCondition('storage_class.class_id', $class['class_id'])
    ->execute()
    ->fetchAssoc();

  $rows[] = array(t('Files'), $class_info['files']);
  $rows[] = array(t('Size'), storage_api_format_size($class_info['size']));
  
  foreach($rows as &$row) {
    $row[0] = array(
      'data' => $row[0] . ':',
      'style' => 'font-weight: bold',
    );
  }
  
  $output = theme('table', array(
    'header' => NULL, 
    'rows' => $rows,
    'attributes' => array('style' => 'width: auto;'),
  ));

  unset($rows);
  
  $objects_subquery = db_select('storage_selector');
  $objects_subquery->join('storage_file', NULL, 'storage_selector.selector_id = storage_file.selector_id');
  $objects_subquery->join('storage_object', NULL, 'storage_file.object_id = storage_object.object_id');
  $objects_subquery->join('storage_object_instance', NULL, 'storage_object.object_id = storage_object_instance.object_id');
    
  $objects_subquery->fields('storage_object_instance', 'container_id');
  $objects_subquery->addExpression('COUNT(storage_file.file_id)', 'count');
  $objects_subquery->addExpression('SUM(storage_object.size)', 'size');
  
  $objects_subquery->condition('storage_selector.class_id', $class['class_id'])
    ->groupBy('storage_object_instance.container_id');
    
    
  $files_subquery = db_select('storage_selector');
  $files_subquery->join('storage_file', NULL, 'storage_selector.selector_id = storage_file.selector_id');
  $files_subquery->join('storage_object', NULL, 'storage_file.object_id = storage_object.object_id');
  $files_subquery->join('storage_file_instance', NULL, 'storage_file.file_id = storage_file_instance.file_id');
  
  $files_subquery->fields('storage_object_instance', 'container_id');
  $files_subquery->addExpression('COUNT(storage_file.file_id)', 'count');
  $files_subquery->addExpression('SUM(storage_object.size)', 'size');
    
  $files_subquery->condition('storage_selector.class_id', $class['class_id'])
    ->groupBy('storage_file_instance.container_id');
    
    
  $query = db_select('storage_class_container', NULL, array('fetch' => PDO::FETCH_ASSOC));
  $query->join('storage_container', NULL, 'storage_class_container.container_id = storage_container.container_id');
  $query->leftJoin($objects_subquery, 'objects', 'storage_container.container_id = objects.container_id');
  $query->leftJoin($files_subquery, 'files', 'storage_container.container_id = files.container_id');

  $result = $query->condition('storage_class_container.class_id', $class['class_id'])
    ->orderBy('storage_class_container.weight')
    ->orderBy('storage_container.container_id')
    ->execute();


  foreach($result as $container) {
    unset($row);
    
    $row[] = l($container['name'], 
        'admin/build/storage/containers/' . $container['container_id']);
    $row[] = l(_storage_api_get_service_name($container['service_id']), 
        'admin/build/storage/services/' . $container['service_id']);
    $row[] = $container['serving'] ? t('Yes') : t('No');
    $row[] = $container['objects_count'] + $container['files_count'];
    $row[] = storage_api_format_size($container['objects_size'] + $container['files_size']);
    
    $rows[] = $row;
  }

  if(empty($rows)) {
    $rows[] = array(array('data' => t('There are no containers in this class.'), 'colspan' => 5, 'class' => 'message'));
  }

  $header = array(t('Container'), t('Service'), t('Serving'), t('Files'), t('Size'));
  
  $output .= theme('table', array(
    'header' => $header, 
    'rows' => $rows,
    'attributes' => array('style' => 'width: auto;'),
  ));

  if(count($rows) > 1)
    $output .= '<p>' . t('Files are served from the container nearest the top that is capable.') . '</p>';

  return $output;
}


/**
 * FAPI definition for the class edit form.
 *
 * @ingroup forms
 * @see storage_api_class_edit_form_submit()
 */
function storage_api_class_edit_form($form_state, $class) {

  drupal_set_title(t('Storage class: ') . $class['name'], PASS_THROUGH);

  $form = _storage_api_class_form_elements($class);
  
  $query = db_select('storage_class_container', NULL, array('fetch' => PDO::FETCH_ASSOC));
  $query->join('storage_container', NULL, 'storage_class_container.container_id = storage_container.container_id');

  $result = $query->fields('storage_container', array('container_id', 'name', 'service_id'))
    ->fields('storage_class_container', array('weight', 'serving'))
    ->condition('storage_class_container.class_id', $class['class_id'])
    ->orderBy('storage_class_container.weight')
    ->orderBy('storage_container.container_id')
    ->execute();

  $form['#tree'] = TRUE;
  
  $trash = _storage_api_trash_icon();

  foreach($result as $container) {
  
    $form['#container_ids'][] = $container['container_id'];

    $form['names'][$container['container_id']] = array(
      '#markup' => l($container['name'], 'admin/build/storage/containers/' . $container['container_id'])
    );
    
    $form['weights'][$container['container_id']] = array(
      '#type' => 'weight', 
      '#default_value' => $container['weight'],
      '#attributes' => array('class' => "my-elements-weight")

    );

    $form['services'][$container['container_id']] = array(
      '#markup' => l(_storage_api_get_service_name($container['service_id']), 
        'admin/build/storage/services/' . $container['service_id'])
    );
    
    $form['servings'][$container['container_id']] = array(
      '#type' => 'select',
      '#options' => array(
        0 => t('No'),
        1 => t('Yes'),
      ),
      '#default_value' => $container['serving'] ? 1 : 0
    );

    $form['deletes'][$container['container_id']] = array(
      '#markup' => 
        l($trash, 'admin/build/storage/classes/' . $class['class_id'] . '/remove/' . $container['container_id'], 
          array('html' => TRUE))
    );
  }

  if(count($form['#container_ids']) > 1) {
  
    $form['info'] = array(
      '#markup' => '<p>' . t('Files are served from the container nearest the top that is capable.') . '</p>',
    );
  }
  
  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Save')
  );

  $form['destroy'] = array(
    '#type' => 'submit',
    '#value' => t('Destroy'),
    '#submit' => array('storage_api_class_edit_form_destroy')
  );
  
  return $form;
}


/**
 * Themes the class edit form.
 *
 * @ingroup themeable
 * @see storage_api_class_edit_form()
 */
function theme_storage_api_class_edit_form($form) {

  $output .= drupal_render($form['name']);
  $output .= drupal_render($form['options']);

  if(is_array($form['#container_ids'])) {
  
    $header = array(t('Container'), t('Service'), t('Weight'), t('Serving'), NULL);

    foreach($form['#container_ids'] as $container_id) {
      unset($row);
      
      $row[] = drupal_render($form['names'][$container_id]);
      $row[] = drupal_render($form['services'][$container_id]);
      $row[] = drupal_render($form['weights'][$container_id]);
      $row[] = drupal_render($form['servings'][$container_id]);
      $row[] = drupal_render($form['deletes'][$container_id]);
      
      $rows[] = array(
        'data' => $row,
        'class' => 'draggable',
      );
    }
  }

  $output .= theme('table', array(
    'header' => $header, 
    'rows' => $rows,
    'attributes' => array('id' => 'my-module-table', 'style' => 'width: auto;'),
  ));

  $output .= drupal_render($form);
  
  drupal_add_tabledrag('my-module-table', 'order', 'sibling', 'my-elements-weight');

  return $output;
}


/**
 * Submit handler for the class edit form.
 *
 * @see storage_api_class_edit_form()
 */
function storage_api_class_edit_form_submit($form, &$form_state) {
  $class = $form['#parameters'][2];

  $update = array(
    'class_id' => $class['class_id'],
    'name' => $form_state['values']['name'],
    'options' => $form_state['values']['options'],
  );
  
  drupal_write_record('storage_class', $update, 'class_id');
  
  foreach($form['#container_ids'] as $container_id) {

    $update = array(
      'class_id' => $class['class_id'],
      'container_id' => $container_id,
      'weight' => $form_state['values']['weights'][$container_id],
      'serving' => $form_state['values']['servings'][$container_id],
    );

    drupal_write_record('storage_class_container', $update, array('class_id', 'container_id'));
  }
  
  $selector_subquery = db_select('storage_selector')
    ->fields('storage_selector', array('selector_id'))
    ->condition('class_id', $class['class_id']);

  db_update('storage_file')
    ->expression('serve_url', 'NULL')
    ->condition('selector_id', $selector_subquery, 'IN')
    ->execute();
  
  cache_clear_all();
  
  $message = 'Storage class <i>' . $class['name'] . '</i> has been updated.';
  $path = 'admin/build/storage/classes/' . $class['class_id'];

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $path));

  $form_state['redirect'] = $path;
}


/**
 * Submit handler (destroy) for the class edit form.
 *
 * @see storage_api_class_edit_form()
 */
function storage_api_class_edit_form_destroy($form, &$form_state) {
  $class = $form['#parameters'][2];

  $form_state['redirect'] = 'admin/build/storage/classes/' . $class['class_id'] . '/destroy';
}


/**
 * FAPI definition for the class destroy form.
 *
 * @ingroup forms
 * @see storage_api_class_destroy_form_submit()
 */
function storage_api_class_destroy_form($form_state, $class) {

  return confirm_form($form, 'Are you sure you want to destroy storage class <i>' . 
    $class['name'] . '</i> ?', 'admin/build/storage/classes/' . $class['class_id'], t(''));
}


/**
 * Submit handler for the class destroy form.
 *
 * @see storage_api_class_destroy_form()
 */
function storage_api_class_destroy_form_submit($form, &$form_state) {
  $class = $form['#parameters'][2];
  
  db_delete('storage_class')
    ->condition('class_id', $class['class_id'])
    ->execute();

  $message = 'Storage class <i>' . _storage_api_class_name($class['class_id']) . '</i> has been destroyed.';

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL);

  $form_state['redirect'] = 'admin/build/storage/classes';
}


/**
 * FAPI definition for the class add container form.
 *
 * @ingroup forms
 * @see storage_api_class_add_container_form_submit()
 */
function storage_api_class_add_container_form($form_state, $class) {

  drupal_set_title(t('Storage class: ') . $class['name'], PASS_THROUGH);

  $selector_subquery = db_select('storage_class_container')
    ->fields('storage_class_container', array('container_id'))
    ->condition('class_id', $class['class_id']);

  $result = db_select('storage_container', NULL, array('fetch' => PDO::FETCH_ASSOC))
    ->fields('storage_container')
    ->condition('container_id', $selector_subquery, 'NOT IN')
    ->orderBy('name')
    ->orderBy('container_id')
    ->execute();
 
  foreach($result as $container) {
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
    '#options' => $containers
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
 * @see storage_api_class_add_container_form()
 */
function storage_api_class_add_container_form_submit($form, &$form_state) {
  $class = $form['#parameters'][2];
  $container = storage_api_container_load($form_state['values']['container_id']);
  
  storage_api_class_add_container($class, $container);
  
  $form_state['redirect'] = 'admin/build/storage/classes/' . $class['class_id'] . '/edit';
}


/**
 * FAPI definition for the class remove container form.
 *
 * @ingroup forms
 * @see storage_api_class_remove_container_form_submit()
 */
function storage_api_class_remove_container_form($form_state, $class, $container) {

  $form['#class_id'] = $class['class_id'];
  $form['#container_id'] = $container['container_id'];

  return confirm_form($form, 'Are you sure you want to remove storage container <i>' . $container['name'] . 
    '</i> from class <i>' . $class['name'] . '</i>?', 
    'admin/build/storage/classes/' . $class['class_id'] . '/edit', t(''));
}


/**
 * Submit handler for the class remove container form.
 *
 * @see storage_api_class_remove_container_form()
 */
function storage_api_class_remove_container_form_submit($form, &$form_state) {
  $class = $form['#parameters'][2];
  $container = $form['#parameters'][3];

  db_delete('storage_class_container')
    ->condition('class_id', $class['class_id'])
    ->condition('container_id', $container['container_id'])
    ->execute();
  
  $selector_subquery = db_select('storage_selector')
    ->fields('storage_selector', array('selector_id'))
    ->condition('class_id', $class['class_id']);

  db_update('storage_file')
    ->fields(array('check_me' => 1))
    ->expression('serve_url', 'NULL')
    ->condition('selector_id', $selector_subquery, 'IN')
    ->execute();

  cache_clear_all();

  $container = storage_api_container_load($container['container_id']);
  $path = 'admin/build/storage/classes/' . $class['class_id'];
  
  $message = 'Storage container <i>' . $container['name'] . 
    '</i> has been removed from class <i>' . $class['name'] .'</i>.';
    
  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $path));
  
  $form_state['redirect'] = $path;
}

