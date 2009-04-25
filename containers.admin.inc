<?php
// $Id$

// Copyright 2008-2009 Jonathan Brown


function _storage_api_containers() {

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
  
  return theme('table', $header, $rows);
}


function storage_api_create_container_form($form_state) {

  drupal_set_title(t('Storage containers'));
  
  if(!isset($form_state)) {
    $step = 1;
  }
  else {
    if($form_state['clicked_button']['#value'] == 'Back') {
      $step = $form_state['values']['step'] - 1;
    }
    else {
      $step = $form_state['values']['step'];
      
      if(!form_get_errors())
        $step++;
    }
  }

  $form['step'] = array(
    '#type' => 'hidden',
    '#value' => $step
  );
  
  switch($step) {
    
    case 1:

      $form['name'] = array(
        '#title' => t('Name'),
        '#type' => 'textfield',
        '#required' => TRUE,
        '#default_value' => $form_state['storage']['name']
      );
      
      $form['service_id'] = array(
        '#type' => 'select',
        '#title' => t('Service'),
        '#options' => _storage_api_get_service_names(),
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
        '#type' => 'button',
        '#value' => t('Next')
      );

      break;
      
      
    case 2:
    
      $service_name = _storage_api_get_service_name($form_state['values']['service_id']);
    
      switch($form_state['values']['serving']) {

        case _STORAGE_API_STORAGE_ONLY:
          $serving = t('No');
          break;

        case _STORAGE_API_SERVING:
          $serving = t('Yes');
          break;

        case _STORAGE_API_ACCESS_CONTROL:
          $serving = t('Securely');
          break;
      }


      $form[] = array(
        '#value' => '<p>Name: ' . $form_state['values']['name'] . 
          '<br />Service: ' . $service_name . 
          '<br />Serving: ' . $serving . '</p>'
      );
    
      $form['settings'] = array(
        '#type' => 'fieldset',
        '#title' => $service_name . t(' settings'),
        '#tree' => TRUE
      );
      
      $form['settings'] += _storage_api_service_invoke($form_state['values']['service_id'], 'container_settings_form');
      
      $form['back'] = array(
        '#type' => 'button',
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


function storage_api_create_container_form_validate($form, &$form_state) {

  if($form_state['clicked_button']['#value'] == 'Back')
    return;

  switch($form_state['values']['step']) {
  
    case 1:

      if($form_state['values']['serving'] && $form_state['values']['access_control'])
        $form_state['values']['serving'] = _STORAGE_API_ACCESS_CONTROL;
    
      $form_state['storage'] = array(
        'name' => $form_state['values']['name'],
        'service_id' => $form_state['values']['service_id'],
        'serving' => $form_state['values']['serving']
      );
      
      break;
      
      
    case 2:
    
      $form_state['storage']['settings'] = $form_state['values']['settings'];
    
      _storage_api_service_invoke($form_state['storage']['service_id'], 'container_validate', $form_state['storage']);

      break;
  }
}


function storage_api_create_container_form_submit($form, &$form_state) {

  _storage_api_service_invoke($form_state['storage']['service_id'], 'container_create', $form_state['storage']);

  db_query("
    INSERT INTO {storage_container}
    SET name = '%s',
      service_id = '%s',
      serving = %d,
      settings = '%s'
  ",
    $form_state['storage']['name'],
    $form_state['storage']['service_id'],
    $form_state['storage']['serving'],
    serialize($form_state['storage']['settings'])
  );
  
  $container_id = db_last_insert_id('storage_container', 'container_id');
  $path = 'admin/build/storage/containers/' . $container_id;
  
  $message = 'Storage container <i>' . $form_state['storage']['name'] . '</i> has been created.';

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $path));

  unset($form_state['storage']);
  $form_state['rebuild'] = FALSE;
  $form_state['redirect'] = $path;
}


function storage_api_container_form($form_state, $container) {

  drupal_set_title(t('Storage container: ') . $container['name']);

  switch($container['serving']) {
  
    case _STORAGE_API_STORAGE_ONLY:
      $serving = t('No');
      break;

    case _STORAGE_API_SERVING:
      $serving = t('Yes');
      break;

    case _STORAGE_API_ACCESS_CONTROL:
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

  $form['destroy'] = array(
    '#type' => 'submit',
    '#value' => t('Destroy'),
    '#submit' => array('storage_api_container_form_destroy')
  );

  return $form;
}


function storage_api_container_form_suspend($form, &$form_state) {
  $container = $form['#parameters'][2];

  $form_state['redirect'] = 'admin/build/storage/containers/' . $container['container_id'] . '/suspend';
}


function storage_api_container_form_unsuspend($form, &$form_state) {
  $container = $form['#parameters'][2];

  $form_state['redirect'] = 'admin/build/storage/containers/' . $container['container_id'] . '/unsuspend';
}


function storage_api_container_form_destroy($form, &$form_state) {
  $container = $form['#parameters'][2];

  $form_state['redirect'] = 'admin/build/storage/containers/' . $container['container_id'] . '/destroy';
}


function storage_api_container_suspend_form($form_state, $container) {

  return confirm_form($form, 'Are you sure you want to suspend storage container <i>' . $container['name'] . '</i> ?', 
    'admin/build/storage/containers/' . $container['container_id'], t(''));
}


function storage_api_container_suspend_form_submit($form, &$form_state) {
  $container = $form['#parameters'][2];

  db_query("
    UPDATE {storage_container}
    SET suspended = 1
    WHERE container_id = %d
  ",
    $container['container_id']
  );
  
  $path = 'admin/build/storage/containers/' . $container['container_id'];
  
  $message = 'Storage container <i>' . $container['name'] . '</i> has been suspended.';

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $path));

  $form_state['redirect'] = $path;
}


function storage_api_container_unsuspend_form($form_state, $container) {

  return confirm_form($form, 'Are you sure you want to unsuspend storage container <i>' . 
    $container['name'] . '</i> ?', 'admin/build/storage/containers/' . $container['container_id'], t(''));
}


function storage_api_container_unsuspend_form_submit($form, &$form_state) {
  $container = $form['#parameters'][2];

  db_query("
    UPDATE {storage_container}
    SET suspended = 0
    WHERE container_id = %d
  ",
    $container['container_id']
  );
  
  $path = 'admin/build/storage/containers/' . $container['container_id'];
  
  $message = 'Storage container <i>' . $container['name'] . '</i> has been unsuspended.';

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $path));
  
  $form_state['redirect'] = $path;
}


function storage_api_container_destroy_form($form_state, $container) {

  return confirm_form($form, 'Are you sure you want to destroy storage container <i>' . 
    $container['name'] . '</i> ?', 'admin/build/storage/containers/' . $container['container_id'], t(''));
}


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
  
  $success = _storage_api_service_invoke($container['service_id'], 'container_destroy', $container);

  $message = 'Storage container <i>' . $container['name'] . '</i> has been destroyed.';

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL);

  $form_state['redirect'] = 'admin/build/storage/containers';
}


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


function storage_api_container_edit_form_submit($form, &$form_state) {
  $container = $form['#parameters'][2];

  db_query("
    UPDATE {storage_container}
    SET name = '%s'
    WHERE container_id = %d
  ",
    $form_state['values']['name'],
    $container['container_id']
  );
  
  $form_state['redirect'] = 'admin/build/storage/containers/' . $container['container_id'];
}

