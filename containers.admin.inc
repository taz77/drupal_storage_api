<?php
// $Id$

// Copyright 2008-2009 Jonathan Brown


function _storage_api_containers_query() {

  $objects_subquery = db_select('storage_object_instance');
  $objects_subquery->join('storage_object', NULL, 'storage_object_instance.object_id = storage_object.object_id');
  
  $objects_subquery->fields('storage_object_instance', array('container_id'));
  $objects_subquery->addExpression('COUNT(storage_object.object_id)', 'count');
  $objects_subquery->addExpression('SUM(storage_object.size)', 'size');
  
  $objects_subquery->groupBy('storage_object_instance.container_id');


  $files_subquery = db_select('storage_file_instance');
  $files_subquery->join('storage_file', NULL, 'storage_file_instance.file_id = storage_file.file_id');
  $files_subquery->join('storage_object', NULL, 'storage_file.object_id = storage_object.object_id');
  
  $files_subquery->fields('storage_file_instance', array('container_id'));
  $files_subquery->addExpression('COUNT(storage_file.file_id)', 'count');
  $files_subquery->addExpression('SUM(storage_object.size)', 'size');
  
  $files_subquery->groupBy('storage_file_instance.container_id');


  $query = db_select('storage_container', NULL, array('fetch' => PDO::FETCH_ASSOC));
  $query->leftJoin($objects_subquery, 'objects', 'storage_container.container_id = objects.container_id');
  $query->leftJoin($files_subquery, 'files', 'storage_container.container_id = files.container_id');
  
  $query->fields('storage_container');
    
  $query->addField('objects', 'count', 'objects_count');
  $query->addField('objects', 'size', 'objects_size');

  $query->addField('files', 'count', 'files_count');
  $query->addField('files', 'size', 'files_size');
  
  return $query;
}


function storage_api_containers() {

  drupal_set_title(t('Storage containers'), PASS_THROUGH);

  $header = array(t('Container'), t('Service'), t('Serving'), t('Suspended'), t('Instances'), t('Size'));

  $result = _storage_api_containers_query()
    ->orderBy('storage_container.name')
    ->orderBy('storage_container.container_id')
    ->execute();

  foreach($result as $container) {
    unset($row);
  
    $row[] = _storage_api_container_link($container);
    $row[] = _storage_api_get_service_name($container['service_id'], TRUE);
    $row[] = $container['serving'] ? 'Yes' : 'No';
    $row[] = $container['suspended'] ? 'Yes' : 'No';
    $row[] = $container['objects_count'] + $container['files_count'];
    $row[] = storage_api_format_size($container['objects_size'] + $container['files_size']);
    
    $rows[] = $row;
  }
  
  if(empty($rows)) {
    $rows[] = array(array('data' => t('No containers exist.'), 'colspan' => 6, 'class' => 'message'));
  }

  return theme('table', array(
    'header' => $header, 
    'rows' => $rows,
    'attributes' => array('style' => 'width: auto;'),
  ));
}


/**
 * FAPI definition for the create container form.
 *
 * @ingroup forms
 * @see storage_api_create_container_form_validate()
 * @see storage_api_create_container_form_submit()
 */
function storage_api_create_container_form($form, &$form_state) {

  drupal_set_title(t('Storage containers'), PASS_THROUGH);

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
          '#markup' => '<p>There are no storage services available. Please enable a service module on the ' . 
            l('modules page', 'admin/modules') . '.</p>'
        );
        
        break;
      }

      $form['name'] = array(
        '#title' => t('Name'),
        '#type' => 'textfield',
        '#required' => TRUE,
        '#default_value' => isset($form_state['storage']['name']) ? $form_state['storage']['name'] : NULL,
      );
      
      $form['service_id'] = array(
        '#type' => 'select',
        '#title' => t('Service'),
        '#options' => $services,
        '#default_value' => isset($form_state['storage']['service_id']) ? $form_state['storage']['service_id'] : NULL,
      );
      
      $form['serving'] = array(
        '#type' => 'checkbox',
        '#title' => t('Serving'),
        '#default_value' => isset($form_state['storage']['serving']) ? $form_state['storage']['serving'] : NULL,
      );
      
      $form['access_control'] = array(
        '#type' => 'checkbox',
        '#title' => t('Enforce access control'),
        '#default_value' => isset($form_state['storage']['access_control']) ? $form_state['storage']['access_control'] : NULL
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

      $info = array(
        t('Name') => $form_state['storage']['name'],
        t('Service') => $service_name,
        t('Serving') => $serving,
      );

      $form['info'] = array(
        '#markup' => theme_storage_api_info_table($info)
      );

      $form['settings'] = array(
        '#type' => 'fieldset',
        '#title' => $service_name . t(' settings'),
        '#tree' => TRUE
      );
      
      $settings = isset($form_state['storage']['settings']) ? $form_state['storage']['settings'] : array();
      
      $form['settings'] += _storage_api_service_invoke($form_state['storage']['service_id'], 
        'container_settings_form', $settings, $form_state['storage']['serving'], FALSE);
      
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

      $form_state['storage']['name'] = $form_state['values']['name'];
      $form_state['storage']['service_id'] = $form_state['values']['service_id'];
      $form_state['storage']['serving'] = $form_state['values']['serving'];
    
      break;
      
      
    case 2:
    
      $form_state['storage']['settings'] = $form_state['values']['settings'];
    
      if($form_state['clicked_button']['#value'] == t('Back'))
        break;

      _storage_api_service_invoke($form_state['storage']['service_id'], 'container_validate', 
        $form_state['storage'], FALSE);

      if(!form_get_errors()) {
        $form_state['storage'] = _storage_api_service_invoke($form_state['storage']['service_id'], 'container_create', 
          $form_state['storage']);
      }
      
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

  if($form_state['clicked_button']['#value'] != t('Create')) {
    $form_state['rebuild'] = TRUE;
    return;
  }
  
  $container = $form_state['storage'];

  drupal_write_record('storage_container', $container);

  _storage_api_service_invoke($container['service_id'], 'container_postcreate', $container);

  $message = 'Storage container <i>' . $form_state['storage']['name'] . '</i> has been created.';
  $path = 'admin/config/media/storage/containers/' . $container['container_id'];

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $path));

  unset($form_state['storage']);
  $form_state['rebuild'] = FALSE;
  $form_state['redirect'] = $path;
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
function storage_api_container_form($form, &$form_state, $container) {
  $form['#container'] = $container;


  drupal_set_title(t('Storage container: ') . $container['name'], PASS_THROUGH);

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
  
  $container_info = _storage_api_containers_query()
    ->condition('storage_container.container_id', $container['container_id'])
    ->execute()
    ->fetchAssoc();

  $info[t('Service')] = _storage_api_get_service_name($container['service_id'], TRUE);
  $info[t('Serving')] = $serving;
  $info[t('Suspended')] = $container['suspended'] ? t('Yes') : t('No');
  $info[t('Instances')] = $container_info['objects_count'] + $container_info['files_count'];
  $info[t('Size')] = storage_api_format_size($container_info['objects_size'] + $container_info['files_size']);

  $info += _storage_api_service_invoke($container['service_id'], 'container_info', $container);

  $form['status'] = array(
    '#markup' => theme_storage_api_info_table($info)
  );
  
  if(!$container['suspended']) {

    $form['suspend'] = array(
      '#type' => 'submit',
      '#value' => t('Suspend'),
      '#submit' => array('storage_api_container_form_suspend'),
    );
  }
  else {
    $form['unsuspend'] = array(
      '#type' => 'submit',
      '#value' => t('Unsuspend'),
      '#submit' => array('storage_api_container_form_unsuspend'),
    );
  }

  $form['drain'] = array(
    '#type' => 'submit',
    '#value' => t('Drain'),
    '#submit' => array('storage_api_container_form_drain'),
  );

  $form['destroy'] = array(
    '#type' => 'submit',
    '#value' => t('Destroy'),
    '#submit' => array('storage_api_container_form_destroy'),
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
  $container = $form['#container'];

  $form_state['redirect'] = 'admin/config/media/storage/containers/' . $container['container_id'] . '/suspend';
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
  $container = $form['#container'];

  $form_state['redirect'] = 'admin/config/media/storage/containers/' . $container['container_id'] . '/unsuspend';
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
  $container = $form['#container'];

  $form_state['redirect'] = 'admin/config/media/storage/containers/' . $container['container_id'] . '/drain';
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
  $container = $form['#container'];

  $form_state['redirect'] = 'admin/config/media/storage/containers/' . $container['container_id'] . '/destroy';
}


/**
 * FAPI definition for the container suspend form.
 *
 * @ingroup forms
 * @see storage_api_container_suspend_form_submit()
 */
function storage_api_container_suspend_form($form, &$form_state, $container) {
  $form['#container'] = $container;

  return confirm_form($form, 'Are you sure you want to suspend storage container <i>' . $container['name'] . '</i> ?', 
    'admin/config/media/storage/containers/' . $container['container_id'], t(''));
}


/**
 * Submit handler for the container suspend form.
 *
 * @see storage_api_container_suspend_form()
 */
function storage_api_container_suspend_form_submit($form, &$form_state) {
  $container = $form['#container'];

  $update = array(
    'container_id' => $container['container_id'],
    'suspended' => TRUE,
  );
  
  drupal_write_record('storage_container', $update, 'container_id');
  _storage_api_container_flush_serving_container($container['container_id']);

  $message = 'Storage container <i>' . $container['name'] . '</i> has been suspended.';
  $path = 'admin/config/media/storage/containers/' . $container['container_id'];

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
function storage_api_container_unsuspend_form($form, &$form_state, $container) {
  $form['#container'] = $container;

  return confirm_form($form, 'Are you sure you want to unsuspend storage container <i>' . 
    $container['name'] . '</i> ?', 'admin/config/media/storage/containers/' . $container['container_id'], t(''));
}


/**
 * Submit handler for the container unsuspend form.
 *
 * @see storage_api_container_unsuspend_form()
 */
function storage_api_container_unsuspend_form_submit($form, &$form_state) {
  $container = $form['#container'];

  $update = array(
    'container_id' => $container['container_id'],
    'suspended' => FALSE,
  );
  
  drupal_write_record('storage_container', $update, 'container_id');
  _storage_api_container_flush_serving_container($container['container_id']);

  $message = 'Storage container <i>' . $container['name'] . '</i> has been unsuspended.';
  $path = 'admin/config/media/storage/containers/' . $container['container_id'];

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
function storage_api_container_drain_form($form, &$form_state, $container) {
  $form['#container'] = $container;

  return confirm_form($form, 'Are you sure you want to drain storage container <i>' . 
    $container['name'] . '</i> ?', 'admin/config/media/storage/containers/' . $container['container_id'], t(''));
}


/**
 * Submit handler for the container drain form.
 *
 * @see storage_api_container_drain_form()
 */
function storage_api_container_drain_form_submit($form, &$form_state) {
  $container = $form['#container'];

  storage_api_drain_container($container);
  
  $form_state['redirect'] = 'admin/config/media/storage/containers/' . $container['container_id'];
}


/**
 * FAPI definition for the container destroy form.
 *
 * @ingroup forms
 * @see storage_api_container_destroy_form_submit()
 */
function storage_api_container_destroy_form($form, &$form_state, $container) {
  $form['#container'] = $container;

  return confirm_form($form, 'Are you sure you want to destroy storage container <i>' . 
    $container['name'] . '</i> ?', 'admin/config/media/storage/containers/' . $container['container_id'], t(''));
}


/**
 * Submit handler for the container destroy form.
 *
 * @see storage_api_container_destroy_form()
 */
function storage_api_container_destroy_form_submit($form, &$form_state) {
  $container = $form['#container'];

  db_delete('storage_container')
    ->condition('container_id', $container['container_id'])
    ->execute();

  db_delete('storage_class_container')
    ->condition('container_id', $container['container_id'])
    ->execute();

  db_delete('storage_file_instance')
    ->condition('container_id', $container['container_id'])
    ->execute();

  db_delete('storage_object_instance')
    ->condition('container_id', $container['container_id'])
    ->execute();
  
  _storage_api_container_flush_serving_container($container['container_id']);

  $success = _storage_api_service_invoke($container['service_id'], 'container_destroy', $container);

  $message = 'Storage container <i>' . $container['name'] . '</i> has been destroyed.';

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL);

  $form_state['redirect'] = 'admin/config/media/storage/containers';
}


/**
 * FAPI definition for the container edit form.
 *
 * @ingroup forms
 * @see storage_api_container_edit_form_submit()
 * @see storage_api_container_edit_form_validate()
 */
function storage_api_container_edit_form($form, &$form_state, $container) {
  $form['#container'] = $container;

  $service_name = _storage_api_get_service_name($container['service_id']);

  drupal_set_title(t('Storage container: ') . $container['name'] . 
    ' (' . $service_name . ')', PASS_THROUGH);
  
  $form['name'] = array(
    '#title' => 'Name',
    '#type' => 'textfield',
    '#required' => TRUE,
    '#default_value' => $container['name']
  );

  $form['settings'] = array(
    '#type' => 'fieldset',
    '#title' => $service_name . t(' settings'),
    '#tree' => TRUE
  );
  
  $form['settings'] += _storage_api_service_invoke($container['service_id'], 'container_settings_form', 
    $container['settings'], $container['serving'], TRUE);

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => 'Save'
  );
  
  return $form;
}


/**
 * Validate handler for the container edit form.
 *
 * @see storage_api_container_edit_form()
 * @see storage_api_container_edit_form_submit()
 */
function storage_api_container_edit_form_validate($form, &$form_state) {
  $container = $form['#container'];
  
  $container['settings'] = $form_state['values']['settings'] + $container['settings'];

  _storage_api_service_invoke($container['service_id'], 'container_validate', $container, TRUE);
}


/**
 * Submit handler for the container edit form.
 *
 * @see storage_api_container_edit_form()
 * @see storage_api_container_edit_form_validate()
 */
function storage_api_container_edit_form_submit($form, &$form_state) {
  $container = $form['#container'];

  $container['name'] = $form_state['values']['name'];
  $container['settings'] = $form_state['values']['settings'] + $container['settings'];

  $updated_container = _storage_api_service_invoke($container['service_id'], 'container_update', $container);
  $container = $updated_container ? $updated_container : $container;

  drupal_write_record('storage_container', $container, 'container_id');
  _storage_api_container_flush_serving_container($container['container_id']);

  $message = 'Storage container <i>' . $container['name'] . '</i> has been updated.';
  $path = 'admin/build/storage/containers/' . $container['container_id'];

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $path));

  $form_state['redirect'] = 'admin/build/storage/containers/' . $container['container_id'];
}

