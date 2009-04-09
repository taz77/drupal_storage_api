<?php
// $Id$

// Copyright 2008-2009 Jonathan Brown


_storage_api_update_services();

function _storage_api_trash_icon() {

  return '<img src="' . base_path() . drupal_get_path('module', 'storage_api') . '/trash.png' . 
    '" style="vertical-align: middle;" />';
}



//
// Services
//



function _storage_api_get_service_names() {

  $services = _storage_api_service_invoke_all('service_info');
  
  foreach($services as &$service)
    $service = $service['name'];
  
  return $services;
}


function _storage_api_get_service_name($service_id) {

  $info = _storage_api_service_invoke($service_id, 'service_info');
  
  return $info['name'];
}


function _storage_api_services() {

  drupal_set_title(t('Storage services'));

  $header = array(t('Service'), t('Suspended'), t('Instances'), t('Size'));

  $result = db_query("
    SELECT {storage_service}.*, 
      objects.count AS objects_count, files.count AS files_count, 
      objects.size AS objects_size, files.size AS files_size
    FROM {storage_service}
    LEFT JOIN (
    
      SELECT {storage_container}.service_id, 
        COUNT({storage_object}.object_id) AS count, SUM({storage_object}.size) AS size
      FROM {storage_container}
      INNER JOIN {storage_object_instance}
        ON {storage_container}.container_id = {storage_object_instance}.container_id
      INNER JOIN {storage_object}
        ON {storage_object_instance}.object_id = {storage_object}.object_id
      GROUP BY {storage_container}.service_id
      
    ) AS objects
      ON {storage_service}.service_id = objects.service_id
    LEFT JOIN (
    
      SELECT {storage_container}.service_id, 
        COUNT({storage_file}.file_id) AS count, SUM({storage_object}.size) AS size
      FROM {storage_container}
      INNER JOIN {storage_file_instance}
        ON {storage_container}.container_id = {storage_file_instance}.container_id
      INNER JOIN {storage_file}
        ON {storage_file_instance}.file_id = {storage_file}.file_id
      INNER JOIN {storage_object}
        ON {storage_file}.object_id = {storage_object}.object_id
      GROUP BY {storage_container}.service_id
      
    ) AS files
      ON {storage_service}.service_id = files.service_id
    ORDER BY service_id
  ");


  while($service = db_fetch_array($result)) {
    unset($row);
  
    $name = _storage_api_get_service_name($service['service_id']);
  
    $row[] = l($name, 'admin/build/storage/services/' . $service['service_id']);
    $row[] = $service['suspended'] ? t('Yes') : t('No');
    $row[] = $service['objects_count'] + $service['files_count'];
    $row[] = storage_format_size($service['objects_size'] + $service['files_size']);
    
    $rows[] = $row;
  }
  
  return theme('table', $header, $rows);
}


function storage_api_service_form($form_state, $service) {

  drupal_set_title(t('Storage service: ') . $service['name']);

  $output = t('Suspended: ') . ($service['suspended'] ? t('Yes') : t('No'));
  
  $form['status'] = array(
    '#value' => '<p>' . $output . '</p>'
  );
  
  if(!$service['suspended']) {

    $form['suspend'] = array(
      '#type' => 'submit',
      '#value' => t('Suspend'),
      '#submit' => array('storage_api_service_form_suspend')
    );
  }
  else {
    $form['unsuspend'] = array(
      '#type' => 'submit',
      '#value' => t('Unsuspend'),
      '#submit' => array('storage_api_service_form_unsuspend')
    );
  }

  return $form;
}


function storage_api_service_form_suspend($form, &$form_state) {
  $service = $form['#parameters'][2];

  $form_state['redirect'] = 'admin/build/storage/services/' . $service['service_id'] . '/suspend';
}


function storage_api_service_form_unsuspend($form, &$form_state) {
  $service = $form['#parameters'][2];

  $form_state['redirect'] = 'admin/build/storage/services/' . $service['service_id'] . '/unsuspend';
}


function storage_api_service_suspend_form($form_state, $service) {

  return confirm_form($form, 'Are you sure you want to suspend storage service <i>' . $service['name'] . '</i> ?', 
    'admin/build/storage/services/' . $service['service_id'], t(''));
}


function storage_api_service_suspend_form_submit($form, &$form_state) {

  $service = $form['#parameters'][2];
  
  db_query("
    UPDATE {storage_service}
    SET suspended = 1
    WHERE service_id = '%s'
  ",
    $service['service_id']
  );
  
  $message = 'Storage service <i>' . $service['name'] . '</i> has been suspended.';
  $path = 'admin/build/storage/services/' . $service['service_id']; 

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $path));

  $form_state['redirect'] = $path;
}


function storage_api_service_unsuspend_form($form_state, $service) {

  return confirm_form($form, 'Are you sure you want to unsuspend storage service <i>' . $service['name'] . '</i> ?', 
    'admin/build/storage/services/' . $service['service_id'], t(''));
}


function storage_api_service_unsuspend_form_submit($form, &$form_state) {

  db_query("
    UPDATE {storage_service}
    SET suspended = 0
    WHERE service_id = '%s'
  ",
    $form['#parameters'][2]['service_id']
  );
  
  $path = 'admin/build/storage/services/' . $form['#parameters'][2]['service_id']; 

  $message = 'Storage service <i>' . $form['#parameters'][2]['name'] . '</i> has been unsuspended.';

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $path));

  $form_state['redirect'] = $path;
}



//
// Containers
//



function _storage_api_containers() {

  drupal_set_title(t('Storage containers'));

  $header = array(t('Name'), t('Service'), t('Serving'), t('Suspended'), t('Instances'), t('Size'));

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
    $row[] = storage_format_size($container['objects_size'] + $container['files_size']);
    
    $rows[] = $row;
  }
  
  return theme('table', $header, $rows);
}


function storage_api_create_container_form($form_state) {

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
    
      $form[] = array(
        '#value' => '<p>Name: ' . $form_state['values']['name'] . 
          '<br />Service: ' . $service_name . 
          '<br />Serving: ' . ($form_state['values']['serving'] ? t('Yes') : t('No')) . 
          '<br />Access control: ' . ($form_state['values']['access_control'] ? t('Yes') : t('No')) . '</p>'
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

  $info = array(
    t('Type') => _storage_api_get_service_name($container['service_id']),
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



//
// Classes
//



function _storage_api_class_name($class_id) {

  return db_result(db_query("
    SELECT name
    FROM {storage_class}
    WHERE class_id = %d
  ",
    $class_id
  ));
}


function _storage_api_classes_list() {

  drupal_set_title(t('Storage classes'));

  $result = db_query("
    SELECT {storage_class}.*,
      COUNT({storage_file}.file_id) AS files,
      SUM({storage_object}.size) AS size
    FROM {storage_class}
    LEFT JOIN {storage_selector}
      ON {storage_class}.class_id = {storage_selector}.class_id
    LEFT JOIN {storage_file}
      ON {storage_selector}.selector_id = {storage_file}.selector_id
    LEFT JOIN {storage_object}
      ON {storage_file}.object_id = {storage_object}.object_id
    GROUP BY {storage_class}.class_id
    ORDER BY name, class_id
  ");

  $header = array(
    t('Name'), t('Files'), t('Size')
  );

  while($class = db_fetch_array($result)) {
    unset($row);
    
    $row[] = l($class['name'], 'admin/build/storage/classes/' . $class['class_id']);
    $row[] = $class['files'];
    $row[] = storage_format_size((int)$class['size']);
    
    $rows[] = $row;
  }

  if(empty($rows)) {
    $rows[] = array(array('data' => t('No classes configured.'), 'colspan' => '3', 'class' => 'message'));
  }

  return theme('table', $header, $rows);
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


function storage_api_create_class_form() {

  $form = _storage_api_class_form_elements(); 

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Create class')
  );
  
  return $form;
}


function storage_api_create_class_form_submit($form, &$form_state) {

  db_query("
    INSERT INTO {storage_class}
    SET name = '%s',
      options = '%s'
  ",
    $form_state['values']['name'],
    serialize($form_state['values']['options'])
  );
  
  $class_id = db_last_insert_id('storage_class', 'class_id');
  $path = 'admin/build/storage/classes/' . $class_id . '/add';
  
  $message = 'Storage class <i>' . $form_state['values']['name'] . '</i> has been created.';

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $path));

  $form_state['redirect'] = $path;
}


function _storage_api_class($class) {

  drupal_set_title(t('Storage class: ') . $class['name']);
  

  $result = db_query("
    SELECT {storage_container}.*, {storage_class_container}.serving AS serving, 
      objects.count AS objects_count, objects.size AS objects_size,
      files.count AS files_count, files.size AS files_size
    FROM {storage_class_container}
    INNER JOIN {storage_container}
      ON {storage_class_container}.container_id = {storage_container}.container_id
    LEFT JOIN (
    
      SELECT {storage_object_instance}.container_id, 
        COUNT({storage_file}.file_id) AS count, SUM({storage_object}.size) AS size
      FROM {storage_selector}
      INNER JOIN {storage_file}
        ON {storage_selector}.selector_id = {storage_file}.selector_id
      INNER JOIN {storage_object}
        ON {storage_file}.object_id = {storage_object}.object_id
      INNER JOIN {storage_object_instance}
        ON {storage_object}.object_id = {storage_object_instance}.object_id
      WHERE {storage_selector}.class_id = %d
      GROUP BY {storage_object_instance}.container_id
      
    ) AS objects
      ON {storage_container}.container_id = objects.container_id
    LEFT JOIN (
    
      SELECT {storage_file_instance}.container_id, 
        COUNT({storage_file}.file_id) AS count, SUM({storage_object}.size) AS size
      FROM {storage_selector}
      INNER JOIN {storage_file}
        ON {storage_selector}.selector_id = {storage_file}.selector_id
      INNER JOIN {storage_object}
        ON {storage_file}.object_id = {storage_object}.object_id
      INNER JOIN {storage_file_instance}
        ON {storage_file}.file_id = {storage_file_instance}.file_id
      WHERE {storage_selector}.class_id = %d
      GROUP BY {storage_file_instance}.container_id
      
    ) AS files
      ON {storage_container}.container_id = files.container_id
    WHERE {storage_class_container}.class_id = %d
    ORDER BY {storage_class_container}.weight, {storage_container}.container_id
  ",
    $class['class_id'], $class['class_id'], $class['class_id']
  );

  while($container = db_fetch_array($result)) {
    unset($row);
    
    $row[] = l($container['name'], 
        'admin/build/storage/containers/' . $container['container_id']);
    $row[] = l(_storage_api_get_service_name($container['service_id']), 
        'admin/build/storage/services/' . $container['service_id']);
    $row[] = $container['serving'] ? t('Yes') : t('No');
    $row[] = $container['objects_count'] + $container['files_count'];
    $row[] = storage_format_size($container['objects_size'] + $container['files_size']);
    
    $rows[] = $row;
  }

  $header = array(t('Container'), t('Service'), t('Serving'), t('Files'), t('Size'));
  $output = theme('table', $header, $rows);

  return $output;
}


function storage_api_class_edit_form($form_state, $class) {

  drupal_set_title(t('Storage class: ') . $class['name']);

  $form = _storage_api_class_form_elements($class);
  
  $result = db_query("
    SELECT {storage_container}.container_id, {storage_container}.name, {storage_container}.service_id, 
      {storage_class_container}.weight, {storage_class_container}.serving
    FROM {storage_class_container}
    INNER JOIN {storage_container}
      ON {storage_class_container}.container_id = {storage_container}.container_id
    WHERE class_id = %d
    ORDER BY {storage_class_container}.weight, {storage_container}.container_id
  ",
    $class['class_id']
  );

  $form['container_ids'] = array('#tree' => TRUE);
  $form['weights'] = array('#tree' => TRUE);
  $form['servings'] = array('#tree' => TRUE);
  
  $trash = _storage_api_trash_icon();

  $form['#container_ids'] = array();
  
  while($container = db_fetch_array($result)) {
  
    $form['#container_ids'][] = $container['container_id'];

    $form['names'][$container['container_id']] = array(
      '#value' => l($container['name'], 
        'admin/build/storage/containers/' . $container['container_id'])
    );
    
    $form['weights'][$container['container_id']] = array(
      '#type' => 'weight', 
      '#default_value' => $container['weight'],
      '#attributes' => array('class' => "my-elements-weight")

    );

    $form['services'][$container['container_id']] = array(
      '#value' => l(_storage_api_get_service_name($container['service_id']), 
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
      '#value' => l($trash, 'admin/build/storage/classes/' . $class['class_id'] . '/remove/' . $container['container_id'], 
        array('html' => TRUE))
    );
  }

//  $form['#theme'] = 'storage_api_class_edit_form'; // this will be called as theme('my_shared_form').
 
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


function theme_storage_api_class_edit_form($form) {

  $output .= drupal_render($form['name']);
  $output .= drupal_render($form['options']);

  $header = array(t('Container'), t('Service'), t('Weight'), t('Serving'), NULL);
  $rows = array();
  foreach(element_children($form['names']) as $id) {
    // Don't take form control structures
    if (is_array($form['names'][$id])) {
      unset($row);
      
      $row[] = drupal_render($form['names'][$id]);
      $row[] = drupal_render($form['services'][$id]);
      $row[] = drupal_render($form['weights'][$id]);
      $row[] = drupal_render($form['servings'][$id]);
      $row[] = drupal_render($form['deletes'][$id]);
      
      $rows[] = array(
        'data' => $row,
        'class' => 'draggable',
      );
    }
  }

  $output .= theme('table', $header, $rows, array('id' => 'my-module-table'));
  
  $output .= drupal_render($form);
  
  drupal_add_tabledrag('my-module-table', 'order', 'sibling', 'my-elements-weight');

  return $output;
}


function storage_api_class_edit_form_submit($form, &$form_state) {
  $class = $form['#parameters'][2];

  db_query("
    UPDATE {storage_class}
    SET name = '%s',
      options = '%s'
    WHERE class_id = %d
  ",
    $form_state['values']['name'],
    serialize($form_state['values']['options']),
    $class['class_id']
  );
  
  foreach($form['#container_ids'] as $container_id) {

    db_query("
      UPDATE {storage_class_container}
      SET weight = %d,
        serving = %d
      WHERE class_id = %d
        AND container_id = %d
    ", 
      $form_state['values']['weights'][$container_id],
      $form_state['values']['servings'][$container_id],
      $class['class_id'], $container_id
    );
  }
  
  $path = 'admin/build/storage/classes/' . $class['class_id'];

  $message = 'Storage class <i>' . $class['name'] . '</i> has been updated.';

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $path));

  $form_state['redirect'] = $path;
}


function storage_api_class_edit_form_destroy($form, &$form_state) {
  $class = $form['#parameters'][2];

  $form_state['redirect'] = 'admin/build/storage/classes/' . $class['class_id'] . '/destroy';
}


function storage_api_class_destroy_form($form_state, $class) {

  return confirm_form($form, 'Are you sure you want to destroy storage class <i>' . 
    $class['name'] . '</i> ?', 'admin/build/storage/classes/' . $class['class_id'], t(''));
}


function storage_api_class_destroy_form_submit($form, &$form_state) {
  $class = $form['#parameters'][2];
  
  db_query("
    DELETE FROM {storage_class}
    WHERE class_id = %d
  ",
    $class['class_id']
  );

  $message = 'Storage class <i>' . _storage_api_class_name($class['class_id']) . '</i> has been destroyed.';

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL);

  $form_state['redirect'] = 'admin/build/storage/classes';
}


function storage_api_class_add_container_form($form_state, $class) {

  drupal_set_title(t('Storage class: ') . $class['name']);

  $result = db_query("
    SELECT *
    FROM {storage_container}
    WHERE container_id NOT IN (
    
      SELECT container_id
      FROM {storage_class_container}
      WHERE class_id = %d
      
    )
    ORDER BY name, container_id
  ",
    $class['class_id']
  );
  
  while($container = db_fetch_array($result)) {
    $containers[$container['container_id']] = $container['name'];
  }
  
  if(count($containers) == 0) {
  
    $form['class_id'] = array(
      '#value' => '<p>There are no containers left to add to the class.</p>'
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


function storage_api_class_add_container_form_submit($form, &$form_state) {
  $class = $form['#parameters'][2];

  db_query("
    INSERT IGNORE INTO {storage_class_container}
    SET class_id = %d,
      container_id = %d
  ",
    $class['class_id'],
    $form_state['values']['container_id']
  );
  
  db_query("
    UPDATE {storage_file}
    SET check_me = 1
    WHERE selector_id IN (
      SELECT selector_id
      FROM {storage_selector}
      WHERE class_id = %d
    )
  ",
    $class['class_id']
  );

  $container = storage_api_container_load($form_state['values']['container_id']);
  $path = 'admin/build/storage/classes/' . $class['class_id'];  
  
  $message = 'Storage container <i>' . $container['name'] . 
    '</i> has been added to class <i>' . $class['name'] .'</i>.';
  
  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $path));

  $form_state['redirect'] = $path . '/edit';
}


function storage_api_class_remove_container_form($form_state, $class, $container) {

  $form['#class_id'] = $class['class_id'];
  $form['#container_id'] = $container['container_id'];

  return confirm_form($form, 'Are you sure you want to remove storage container <i>' . $container['name'] . 
    '</i> from class <i>' . $class['name'] . '</i>?', 
    'admin/build/storage/classes/' . $class['class_id'] . '/edit', t(''));
}


function storage_api_class_remove_container_form_submit($form, &$form_state) {
  $class = $form['#parameters'][2];
  $container = $form['#parameters'][3];

  db_query("
    DELETE FROM {storage_class_container}
    WHERE class_id = %d
      AND container_id = %d
  ",
    $class['class_id'],
    $container['container_id']
  );
  
  db_query("
    UPDATE {storage_file}
    SET check_me = 1
    WHERE selector_id IN (
    
      SELECT selector_id
      FROM {storage_selector}
      WHERE class_id = %d
      
    )
  ",
    $class['class_id']
  );
  
  $container = storage_api_container_load($container['container_id']);
  $path = 'admin/build/storage/classes/' . $class['class_id'];
  
  $message = 'Storage container <i>' . $container['name'] . 
    '</i> has been removed from class <i>' . $class['name'] .'</i>.';
    
  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $path));
  
  $form_state['redirect'] = $path;
}

