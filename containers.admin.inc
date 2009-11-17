<?php
// $Id$

// Copyright 2008-2009 Jonathan Brown


function storage_api_containers() {

  drupal_set_title(t('Storage containers'));

  $header = array(t('Container'), t('Service'), t('Serving'), t('Suspended'), t('Instances'), t('Size'));

  $result = db_query("
    SELECT {storage_container}.*, 
      objects.count AS objects_count, objects.size AS objects_size,
      files.count AS files_count, files.size AS files_size
    FROM {storage_container}
    LEFT JOIN (
    
      SELECT {storage_object_instance}.container_id, 
        COUNT({storage_object}.object_id) AS count, SUM({storage_object}.size) AS size
      FROM {storage_object_instance}
      INNER JOIN {storage_object}
        ON {storage_object_instance}.object_id = {storage_object}.object_id
      GROUP BY {storage_object_instance}.container_id
      
    ) AS objects
      ON {storage_container}.container_id = objects.container_id
    LEFT JOIN (
    
      SELECT {storage_file_instance}.container_id, 
        COUNT({storage_file}.file_id) AS count, SUM({storage_object}.size) AS size
      FROM {storage_file_instance}
      INNER JOIN {storage_file}
        ON {storage_file_instance}.file_id = {storage_file}.file_id
      INNER JOIN {storage_object}
        ON {storage_file}.object_id = {storage_object}.object_id
      GROUP BY {storage_file_instance}.container_id
      
    ) AS files
      ON {storage_container}.container_id = files.container_id
    ORDER BY name, container_id
  ");

  while($container = db_fetch_array($result)) {
    unset($row);
  
    $row[] = l($container['name'], 'admin/build/storage/containers/' . $container['container_id']);
    $row[] = l(_storage_api_get_service_name($container['service_id']), 
      'admin/build/storage/services/' . $container['service_id']);
    $row[] =  $container['serving'] ? 'Yes' : 'No';
    $row[] =  $container['suspended'] ? 'Yes' : 'No';
    $row[] = $container['objects_count'] + $container['files_count'];
    $row[] = storage_api_format_size($container['objects_size'] + $container['files_size']);
    
    $rows[] = $row;
  }
  
  if(empty($rows)) {
    $rows[] = array(array('data' => t('No containers exist.'), 'colspan' => 6, 'class' => 'message'));
  }

  return theme('table', $header, $rows);
}


/**
 * FAPI definition for the create container form.
 *
 * @ingroup forms
 * @see storage_api_create_container_form_validate()
 * @see storage_api_create_container_form_submit()
 */
function storage_api_create_container_form($form_state) {

  drupal_set_title(t('Storage containers'));
  
  if(!isset($form_state['values']['step'])) {
    $step = 1;
  }
  else {
    if($form_state['clicked_button']['#value'] == t('Back')) {
      $step = $form_state['values']['step'] - 1;
    }
    else {
      $step = $form_state['values']['step'];
      
      if(!form_get_errors())
        $step++;
    }
  }

  $form['step'] = array(
    '#type' => 'value',
    '#value' => $step
  );
  
  switch($step) {
    
    case 1:
    
      $services = _storage_api_get_service_names();
      
      if(count($services) == 0) {
      
        $form[] = array(
          '#value' => '<p>There are no storage services available. Please enable a service module on the ' . 
            l('modules page', 'admin/build/modules') . '.</p>'
        );
        
        break;
      }

      $form['name'] = array(
        '#title' => t('Name'),
        '#type' => 'textfield',
        '#required' => TRUE,
        '#default_value' => $form_state['storage']['name']
      );
      
      $form['service_id'] = array(
        '#type' => 'select',
        '#title' => t('Service'),
        '#options' => $services,
        '#default_value' => $form_state['storage']['service_id']
      );
      
      $form['serving'] = array(
        '#type' => 'checkbox',
        '#title' => t('Serving'),
        '#default_value' => $form_state['storage']['serving']
      );
      
      $form['access_control'] = array(
        '#type' => 'checkbox',
        '#title' => t('Enforce access control'),
        '#default_value' => $form_state['storage']['access_control']
      );

      $form['next'] = array(
        '#type' => 'submit',
        '#value' => t('Next')
      );

      break;
      
      
    case 2:
    
      $service_name = _storage_api_get_service_name($form_state['storage']['service_id']);
    
      switch($form_state['storage']['serving']) {

        case STORAGE_API_STORAGE_ONLY:
          $serving = t('No');
          break;

        case STORAGE_API_SERVING:
          $serving = t('Yes');
          break;

        case STORAGE_API_ACCESS_CONTROL:
          $serving = t('Securely');
          break;
      }
      
      $form[] = array(
        '#value' => '<p>Name: ' . $form_state['storage']['name'] . 
          '<br />Service: ' . $service_name . 
          '<br />Serving: ' . $serving . '</p>'
      );
    
      $form['settings'] = array(
        '#type' => 'fieldset',
        '#title' => $service_name . t(' settings'),
        '#tree' => TRUE
      );
      
      $form['settings'] += _storage_api_service_invoke($form_state['storage']['service_id'], 
        'container_settings_form', $form_state['storage']['settings'], $form_state['storage']['serving']);
      
      $form['back'] = array(
        '#type' => 'submit',
        '#value' => t('Back')
      );
      
      $form['create'] = array(
        '#type' => 'submit',
        '#value' => t('Create')
      );

      break;
  }
  
  return $form;
}


/**
 * Validate handler for the create container form.
 *
 * @see storage_api_create_container_form()
 * @see storage_api_create_container_form_submit()
 */
function storage_api_create_container_form_validate($form, &$form_state) {

  switch($form_state['values']['step']) {
  
    case 1:

      if($form_state['values']['serving'] && $form_state['values']['access_control'])
        $form_state['values']['serving'] = STORAGE_API_ACCESS_CONTROL;
    
      $form_state['storage'] = array(
        'name' => $form_state['values']['name'],
        'service_id' => $form_state['values']['service_id'],
        'serving' => $form_state['values']['serving']
      );
      
      break;
      
      
    case 2:
    
      $form_state['storage']['settings'] = $form_state['values']['settings'];
    
      if($form_state['clicked_button']['#value'] == t('Back'))
        break;

      _storage_api_service_invoke($form_state['storage']['service_id'], 'container_validate', $form_state['storage']);

      break;
  }
}


/**
 * Submit handler for the create container form.
 *
 * @see storage_api_create_container_form()
 * @see storage_api_create_container_form_validate()
 */
function storage_api_create_container_form_submit($form, &$form_state) {

  if($form_state['clicked_button']['#value'] != t('Create'))
    return;

  _storage_api_service_invoke($form_state['storage']['service_id'], 'container_create', $form_state['storage']);

  $container = array(
    'name' => $form_state['storage']['name'],
    'service_id' => $form_state['storage']['service_id'],
    'serving' => $form_state['storage']['serving'],
    'settings' => $form_state['storage']['settings'],
  );
  
  drupal_write_record('storage_container', $container);

  $message = 'Storage container <i>' . $form_state['storage']['name'] . '</i> has been created.';
  $path = 'admin/build/storage/containers/' . $container['container_id'];

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $path));

  unset($form_state['storage']);
  $form_state['rebuild'] = FALSE;
  $form_state['redirect'] = $path;
}


function _storage_api_container_flush_serve_url($container_id) {

  db_query("
    UPDATE {storage_file}
    SET serve_url = NULL
    WHERE selector_id IN (
    
      SELECT {storage_selector}.selector_id
      FROM {storage_class_container}
      INNER JOIN {storage_selector}
        ON {storage_class_container}.class_id = {storage_selector}.class_id
      WHERE {storage_class_container}.container_id = %d
      
    )
  ",
    $container_id
  );
}


/**
 * FAPI definition for the container form.
 *
 * @ingroup forms
 * @see storage_api_container_form_suspend()
 * @see storage_api_container_form_unsuspend()
 * @see storage_api_container_form_drain()
 * @see storage_api_container_form_destroy()
 */
function storage_api_container_form($form_state, $container) {

  drupal_set_title(t('Storage container: ') . $container['name']);

  switch($container['serving']) {
  
    case STORAGE_API_STORAGE_ONLY:
      $serving = t('No');
      break;

    case STORAGE_API_SERVING:
      $serving = t('Yes');
      break;

    case STORAGE_API_ACCESS_CONTROL:
      $serving = t('Securely');
      break;
  }
  
  $info = array(
    t('Service') => _storage_api_get_service_name($container['service_id']),
    t('Serving') => $serving,
    t('Suspended') => ($container['suspended'] ? t('Yes') : t('No'))
  );
  
  $info += _storage_api_service_invoke($container['service_id'], 'container_info', $container);
  
  foreach($info as $key => $item)
    $output[] = $key . ': ' . $item;
  
  $form['status'] = array(
    '#value' => '<p>' . implode("<br />\n", $output) . '</p>'
  );
  
  if(!$container['suspended']) {

    $form['suspend'] = array(
      '#type' => 'submit',
      '#value' => t('Suspend'),
      '#submit' => array('storage_api_container_form_suspend')
    );
  }
  else {
    $form['unsuspend'] = array(
      '#type' => 'submit',
      '#value' => t('Unsuspend'),
      '#submit' => array('storage_api_container_form_unsuspend')
    );
  }

  $form['drain'] = array(
    '#type' => 'submit',
    '#value' => t('Drain'),
    '#submit' => array('storage_api_container_form_drain')
  );

  $form['destroy'] = array(
    '#type' => 'submit',
    '#value' => t('Destroy'),
    '#submit' => array('storage_api_container_form_destroy')
  );

  return $form;
}


/**
 * Submit handler (suspend) for the container form.
 *
 * @see storage_api_container_form()
 * @see storage_api_container_form_unsuspend()
 * @see storage_api_container_form_drain()
 * @see storage_api_container_form_destroy()
 */
function storage_api_container_form_suspend($form, &$form_state) {
  $container = $form['#parameters'][2];

  $form_state['redirect'] = 'admin/build/storage/containers/' . $container['container_id'] . '/suspend';
}


/**
 * Submit handler (unsuspend) for the container form.
 *
 * @see storage_api_container_form()
 * @see storage_api_container_form_suspend()
 * @see storage_api_container_form_drain()
 * @see storage_api_container_form_destroy()
 */
function storage_api_container_form_unsuspend($form, &$form_state) {
  $container = $form['#parameters'][2];

  $form_state['redirect'] = 'admin/build/storage/containers/' . $container['container_id'] . '/unsuspend';
}


/**
 * Submit handler (drain) for the container form.
 *
 * @see storage_api_container_form()
 * @see storage_api_container_form_suspend()
 * @see storage_api_container_form_unsuspend()
 * @see storage_api_container_form_destroy()
 */
function storage_api_container_form_drain($form, &$form_state) {
  $container = $form['#parameters'][2];

  $form_state['redirect'] = 'admin/build/storage/containers/' . $container['container_id'] . '/drain';
}


/**
 * Submit handler (destroy) for the container form.
 *
 * @see storage_api_container_form()
 * @see storage_api_container_form_suspend()
 * @see storage_api_container_form_unsuspend()
 * @see storage_api_container_form_drain()
 */
function storage_api_container_form_destroy($form, &$form_state) {
  $container = $form['#parameters'][2];

  $form_state['redirect'] = 'admin/build/storage/containers/' . $container['container_id'] . '/destroy';
}


/**
 * FAPI definition for the container suspend form.
 *
 * @ingroup forms
 * @see storage_api_container_suspend_form_submit()
 */
function storage_api_container_suspend_form($form_state, $container) {

  return confirm_form($form, 'Are you sure you want to suspend storage container <i>' . $container['name'] . '</i> ?', 
    'admin/build/storage/containers/' . $container['container_id'], t(''));
}


/**
 * Submit handler for the container suspend form.
 *
 * @see storage_api_container_suspend_form()
 */
function storage_api_container_suspend_form_submit($form, &$form_state) {
  $container = $form['#parameters'][2];

  $update = array(
    'container_id' => $container['container_id'],
    'suspended' => TRUE,
  );
  
  drupal_write_record('storage_container', $update, 'container_id');
  _storage_api_container_flush_serve_url($container['container_id']);

  $message = 'Storage container <i>' . $container['name'] . '</i> has been suspended.';
  $path = 'admin/build/storage/containers/' . $container['container_id'];

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $path));

  $form_state['redirect'] = $path;
}


/**
 * FAPI definition for the container unsuspend form.
 *
 * @ingroup forms
 * @see storage_api_container_unsuspend_form_submit()
 */
function storage_api_container_unsuspend_form($form_state, $container) {

  return confirm_form($form, 'Are you sure you want to unsuspend storage container <i>' . 
    $container['name'] . '</i> ?', 'admin/build/storage/containers/' . $container['container_id'], t(''));
}


/**
 * Submit handler for the container unsuspend form.
 *
 * @see storage_api_container_unsuspend_form()
 */
function storage_api_container_unsuspend_form_submit($form, &$form_state) {
  $container = $form['#parameters'][2];

  $update = array(
    'container_id' => $container['container_id'],
    'suspended' => FALSE,
  );
  
  drupal_write_record('storage_container', $update, 'container_id');
  _storage_api_container_flush_serve_url($container['container_id']);

  $message = 'Storage container <i>' . $container['name'] . '</i> has been unsuspended.';
  $path = 'admin/build/storage/containers/' . $container['container_id'];

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $path));
  
  $form_state['redirect'] = $path;
}


/**
 * FAPI definition for the container drain form.
 *
 * @ingroup forms
 * @see storage_api_container_drain_form_submit()
 */
function storage_api_container_drain_form($form_state, $container) {

  return confirm_form($form, 'Are you sure you want to drain storage container <i>' . 
    $container['name'] . '</i> ?', 'admin/build/storage/containers/' . $container['container_id'], t(''));
}


/**
 * Submit handler for the container drain form.
 *
 * @see storage_api_container_drain_form()
 */
function storage_api_container_drain_form_submit($form, &$form_state) {
  $container = $form['#parameters'][2];

  storage_api_drain_container($container);
  
  $form_state['redirect'] = 'admin/build/storage/containers/' . $container['container_id'];
}


/**
 * FAPI definition for the container destroy form.
 *
 * @ingroup forms
 * @see storage_api_container_destroy_form_submit()
 */
function storage_api_container_destroy_form($form_state, $container) {

  return confirm_form($form, 'Are you sure you want to destroy storage container <i>' . 
    $container['name'] . '</i> ?', 'admin/build/storage/containers/' . $container['container_id'], t(''));
}


/**
 * Submit handler for the container destroy form.
 *
 * @see storage_api_container_destroy_form()
 */
function storage_api_container_destroy_form_submit($form, &$form_state) {
  $container = $form['#parameters'][2];

  db_query("
    DELETE FROM {storage_container}
    WHERE container_id = %d
  ",
    $container['container_id']
  );

  db_query("
    DELETE FROM {storage_class_container}
    WHERE container_id = %d
  ",
    $container['container_id']
  );

  db_query("
    DELETE FROM {storage_file_instance}
    WHERE container_id = %d
  ",
    $container['container_id']
  );

  db_query("
    DELETE FROM {storage_object_instance}
    WHERE container_id = %d
  ",
    $container['container_id']
  );
  
  _storage_api_container_flush_serve_url($container['container_id']);

  $success = _storage_api_service_invoke($container['service_id'], 'container_destroy', $container);

  $message = 'Storage container <i>' . $container['name'] . '</i> has been destroyed.';

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL);

  $form_state['redirect'] = 'admin/build/storage/containers';
}


/**
 * FAPI definition for the container edit form.
 *
 * @ingroup forms
 * @see storage_api_container_edit_form_submit()
 */
function storage_api_container_edit_form($form_state, $container) {

  drupal_set_title(t('Storage container: ') . $container['name'] . 
    ' (' . _storage_api_get_service_name($container['service_id']) . ')');
  
  $form['name'] = array(
    '#title' => 'Name',
    '#type' => 'textfield',
    '#required' => TRUE,
    '#default_value' => $container['name']
  );
/*
  $form['settings'] = _storage_api_service_invoke($container['service_id'], 
    'container_settings_form', $container['settings']);
    
  $form['settings']['#tree'] = TRUE;
*/  
  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => 'Save'
  );
  
  return $form;
}


/**
 * Submit handler for the container edit form.
 *
 * @see storage_api_container_edit_form()
 */
function storage_api_container_edit_form_submit($form, &$form_state) {
  $container = $form['#parameters'][2];

  $update = array(
    'container_id' => $container['container_id'],
    'name' => $form_state['values']['name'],
  );
  
  drupal_write_record('storage_container', $update, 'container_id');

  $form_state['redirect'] = 'admin/build/storage/containers/' . $container['container_id'];
}

