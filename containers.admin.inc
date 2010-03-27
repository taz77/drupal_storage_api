<?php
// $Id$

// Copyright 2008-2010 Jonathan Brown


function storage_containers_capabilities() {

  return array(
    STORAGE_API_STORAGE_ONLY => t('Storage only'),
    STORAGE_API_SERVING => t('Serving'),
    STORAGE_API_ACCESS_CONTROL => t('Serving with access control'),
  );
}


function _storage_containers_query() {

  $subquery = db_select('storage_instance');
  $subquery->join('storage', NULL, 'storage_instance.file_id = storage.file_id');
  $subquery->join('storage_file', NULL, 'storage.file_id = storage_file.file_id');
  
  $subquery->fields('storage_instance', array('container_id'));
  $subquery->addExpression('COUNT(storage.storage_id)', 'count');
  $subquery->addExpression('SUM(storage_file.size)', 'size');
  
  $subquery->groupBy('storage_instance.container_id');


  $query = db_select('storage_container', NULL, array('fetch' => PDO::FETCH_ASSOC));
  $query->leftJoin($subquery, 'files', 'storage_container.container_id = files.container_id');
  
  $query->fields('storage_container');
    
  $query->addField('files', 'count', 'count');
  $query->addField('files', 'size', 'size');
  
  return $query;
}


function storage_containers() {

  drupal_set_title(t('Storage containers'), PASS_THROUGH);

  $header = array(t('Container'), t('Service'), t('Capability'), t('Suspended'), t('Instances'), t('Size'));

  $result = _storage_containers_query()
    ->orderBy('storage_container.name')
    ->orderBy('storage_container.container_id')
    ->execute();

  foreach($result as $container) {
    unset($row);
  
    $row[] = _storage_container_link($container);
    $row[] = _storage_get_service_name($container['service_id'], TRUE);

    $capabilities = storage_containers_capabilities();
    $row[] = $capabilities[$container['capability']];
    
    $row[] = $container['suspended'] ? 'Yes' : 'No';
    $row[] = (int)$container['count'];
    $row[] = storage_format_size($container['size']);
    
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
 * @see storage_create_container_form_validate()
 * @see storage_create_container_form_submit()
 */
function storage_create_container_form($form, &$form_state) {

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
    
      $services = _storage_get_service_names();
      
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
      
      $form['capability'] = array(
        '#type' => 'radios',
        '#title' => t('Capability'),
        '#options' => storage_containers_capabilities(),
        '#default_value' => isset($form_state['storage']['capability']) ? $form_state['storage']['capability'] : STORAGE_API_SERVING,
      );

      $form['next'] = array(
        '#type' => 'submit',
        '#value' => t('Next')
      );

      break;
      
      
    case 2:
    
      $service_name = _storage_get_service_name($form_state['storage']['service_id']);
    
      $capabilities = storage_containers_capabilities();

      $info = array(
        t('Name') => $form_state['storage']['name'],
        t('Service') => $service_name,
        t('Capability') => $capabilities[$form_state['storage']['capability']],
      );

      $form['info'] = array(
        '#markup' => theme_storage_info_table($info)
      );

      $form['settings'] = array(
        '#type' => 'fieldset',
        '#title' => $service_name . t(' settings'),
        '#tree' => TRUE
      );
      
      $settings = isset($form_state['storage']['settings']) ? $form_state['storage']['settings'] : array();
      
      $form['settings'] += _storage_service_invoke($form_state['storage']['service_id'], 
        'container_settings_form', $settings, $form_state['storage']['capability'], FALSE);
      
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
 * @see storage_create_container_form()
 * @see storage_create_container_form_submit()
 */
function storage_create_container_form_validate($form, &$form_state) {

  switch($form_state['values']['step']) {
  
    case 1:

      $form_state['storage']['name'] = $form_state['values']['name'];
      $form_state['storage']['service_id'] = $form_state['values']['service_id'];
      $form_state['storage']['capability'] = $form_state['values']['capability'];
    
      break;
      
      
    case 2:
    
      $form_state['storage']['settings'] = $form_state['values']['settings'];
    
      if($form_state['clicked_button']['#value'] == t('Back'))
        break;

      _storage_service_invoke($form_state['storage']['service_id'], 'container_validate', 
        $form_state['storage'], FALSE);

      if(!form_get_errors()) {
        $form_state['storage'] = _storage_service_invoke($form_state['storage']['service_id'], 'container_create', 
          $form_state['storage']);
      }
      
      break;
  }
}


/**
 * Submit handler for the create container form.
 *
 * @see storage_create_container_form()
 * @see storage_create_container_form_validate()
 */
function storage_create_container_form_submit($form, &$form_state) {

  if($form_state['clicked_button']['#value'] != t('Create')) {
    $form_state['rebuild'] = TRUE;
    return;
  }
  
  $container = $form_state['storage'];

  drupal_write_record('storage_container', $container);

  _storage_service_invoke($container['service_id'], 'container_postcreate', $container);

  $message = 'Storage container <i>' . $form_state['storage']['name'] . '</i> has been created.';
  $path = 'admin/structure/storage/containers/' . $container['container_id'];

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
 * @see storage_container_form_suspend()
 * @see storage_container_form_unsuspend()
 * @see storage_container_form_drain()
 * @see storage_container_form_destroy()
 */
function storage_container_form($form, &$form_state, $container) {
  $form['#container'] = $container;


  drupal_set_title(t('Storage container: ') . $container['name'], PASS_THROUGH);

  $capabilities = storage_containers_capabilities();

  $container_info = _storage_containers_query()
    ->condition('storage_container.container_id', $container['container_id'])
    ->execute()
    ->fetchAssoc();

  $info[t('Service')] = _storage_get_service_name($container['service_id'], TRUE);
  $info[t('Capability')] = $capabilities[$container['capability']];
  $info[t('Suspended')] = $container['suspended'] ? t('Yes') : t('No');
  $info[t('Instances')] = (int)$container_info['count'];
  $info[t('Size')] = storage_format_size($container_info['size']);

  $info += _storage_service_invoke($container['service_id'], 'container_info', $container);

  $form['status'] = array(
    '#markup' => theme_storage_info_table($info)
  );
  
  if(!$container['suspended']) {

    $form['suspend'] = array(
      '#type' => 'submit',
      '#value' => t('Suspend'),
      '#submit' => array('storage_container_form_suspend'),
    );
  }
  else {
    $form['unsuspend'] = array(
      '#type' => 'submit',
      '#value' => t('Unsuspend'),
      '#submit' => array('storage_container_form_unsuspend'),
    );
  }

  $form['drain'] = array(
    '#type' => 'submit',
    '#value' => t('Drain'),
    '#submit' => array('storage_container_form_drain'),
  );

  $form['destroy'] = array(
    '#type' => 'submit',
    '#value' => t('Destroy'),
    '#submit' => array('storage_container_form_destroy'),
  );

  return $form;
}


/**
 * Submit handler (suspend) for the container form.
 *
 * @see storage_container_form()
 * @see storage_container_form_unsuspend()
 * @see storage_container_form_drain()
 * @see storage_container_form_destroy()
 */
function storage_container_form_suspend($form, &$form_state) {
  $container = $form['#container'];

  $form_state['redirect'] = 'admin/structure/storage/containers/' . $container['container_id'] . '/suspend';
}


/**
 * Submit handler (unsuspend) for the container form.
 *
 * @see storage_container_form()
 * @see storage_container_form_suspend()
 * @see storage_container_form_drain()
 * @see storage_container_form_destroy()
 */
function storage_container_form_unsuspend($form, &$form_state) {
  $container = $form['#container'];

  $form_state['redirect'] = 'admin/structure/storage/containers/' . $container['container_id'] . '/unsuspend';
}


/**
 * Submit handler (drain) for the container form.
 *
 * @see storage_container_form()
 * @see storage_container_form_suspend()
 * @see storage_container_form_unsuspend()
 * @see storage_container_form_destroy()
 */
function storage_container_form_drain($form, &$form_state) {
  $container = $form['#container'];

  $form_state['redirect'] = 'admin/structure/storage/containers/' . $container['container_id'] . '/drain';
}


/**
 * Submit handler (destroy) for the container form.
 *
 * @see storage_container_form()
 * @see storage_container_form_suspend()
 * @see storage_container_form_unsuspend()
 * @see storage_container_form_drain()
 */
function storage_container_form_destroy($form, &$form_state) {
  $container = $form['#container'];

  $form_state['redirect'] = 'admin/structure/storage/containers/' . $container['container_id'] . '/destroy';
}


/**
 * FAPI definition for the container suspend form.
 *
 * @ingroup forms
 * @see storage_container_suspend_form_submit()
 */
function storage_container_suspend_form($form, &$form_state, $container) {
  $form['#container'] = $container;

  return confirm_form($form, 'Are you sure you want to suspend storage container <i>' . $container['name'] . '</i> ?', 
    'admin/structure/storage/containers/' . $container['container_id'], t(''));
}


/**
 * Submit handler for the container suspend form.
 *
 * @see storage_container_suspend_form()
 */
function storage_container_suspend_form_submit($form, &$form_state) {
  $container = $form['#container'];

  $update = array(
    'container_id' => $container['container_id'],
    'suspended' => TRUE,
  );
  
  drupal_write_record('storage_container', $update, 'container_id');
  _storage_container_flush_serving_container($container['container_id']);

  $message = 'Storage container <i>' . $container['name'] . '</i> has been suspended.';
  $path = 'admin/structure/storage/containers/' . $container['container_id'];

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $path));

  $form_state['redirect'] = $path;
}


/**
 * FAPI definition for the container unsuspend form.
 *
 * @ingroup forms
 * @see storage_container_unsuspend_form_submit()
 */
function storage_container_unsuspend_form($form, &$form_state, $container) {
  $form['#container'] = $container;

  return confirm_form($form, 'Are you sure you want to unsuspend storage container <i>' . 
    $container['name'] . '</i> ?', 'admin/structure/storage/containers/' . $container['container_id'], t(''));
}


/**
 * Submit handler for the container unsuspend form.
 *
 * @see storage_container_unsuspend_form()
 */
function storage_container_unsuspend_form_submit($form, &$form_state) {
  $container = $form['#container'];

  $update = array(
    'container_id' => $container['container_id'],
    'suspended' => FALSE,
  );
  
  drupal_write_record('storage_container', $update, 'container_id');
  _storage_container_flush_serving_container($container['container_id']);

  $message = 'Storage container <i>' . $container['name'] . '</i> has been unsuspended.';
  $path = 'admin/structure/storage/containers/' . $container['container_id'];

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $path));
  
  $form_state['redirect'] = $path;
}


/**
 * FAPI definition for the container drain form.
 *
 * @ingroup forms
 * @see storage_container_drain_form_submit()
 */
function storage_container_drain_form($form, &$form_state, $container) {
  $form['#container'] = $container;

  return confirm_form($form, 'Are you sure you want to drain storage container <i>' . 
    $container['name'] . '</i> ?', 'admin/structure/storage/containers/' . $container['container_id'], t(''));
}


/**
 * Submit handler for the container drain form.
 *
 * @see storage_container_drain_form()
 */
function storage_container_drain_form_submit($form, &$form_state) {
  $container = $form['#container'];

  storage_drain_container($container);
  
  $form_state['redirect'] = 'admin/structure/storage/containers/' . $container['container_id'];
}


/**
 * FAPI definition for the container destroy form.
 *
 * @ingroup forms
 * @see storage_container_destroy_form_submit()
 */
function storage_container_destroy_form($form, &$form_state, $container) {
  $form['#container'] = $container;

  return confirm_form($form, 'Are you sure you want to destroy storage container <i>' . 
    $container['name'] . '</i> ?', 'admin/structure/storage/containers/' . $container['container_id'], t(''));
}


/**
 * Submit handler for the container destroy form.
 *
 * @see storage_container_destroy_form()
 */
function storage_container_destroy_form_submit($form, &$form_state) {
  $container = $form['#container'];

  db_delete('storage_container')
    ->condition('container_id', $container['container_id'])
    ->execute();

  db_delete('storage_class_container')
    ->condition('container_id', $container['container_id'])
    ->execute();

  db_delete('storage_instance')
    ->condition('container_id', $container['container_id'])
    ->execute();

  _storage_container_flush_serving_container($container['container_id']);

  $success = _storage_service_invoke($container['service_id'], 'container_destroy', $container);

  $message = 'Storage container <i>' . $container['name'] . '</i> has been destroyed.';

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL);

  $form_state['redirect'] = 'admin/structure/storage/containers';
}


/**
 * FAPI definition for the container edit form.
 *
 * @ingroup forms
 * @see storage_container_edit_form_submit()
 * @see storage_container_edit_form_validate()
 */
function storage_container_edit_form($form, &$form_state, $container) {
  $form['#container'] = $container;

  $service_name = _storage_get_service_name($container['service_id']);

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
  
  $form['settings'] += _storage_service_invoke($container['service_id'], 'container_settings_form', 
    $container['settings'], $container['capability'], TRUE);

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => 'Save'
  );
  
  return $form;
}


/**
 * Validate handler for the container edit form.
 *
 * @see storage_container_edit_form()
 * @see storage_container_edit_form_submit()
 */
function storage_container_edit_form_validate($form, &$form_state) {
  $container = $form['#container'];
  
  $container['settings'] = $form_state['values']['settings'] + $container['settings'];

  _storage_service_invoke($container['service_id'], 'container_validate', $container, TRUE);
}


/**
 * Submit handler for the container edit form.
 *
 * @see storage_container_edit_form()
 * @see storage_container_edit_form_validate()
 */
function storage_container_edit_form_submit($form, &$form_state) {
  $container = $form['#container'];

  $container['name'] = $form_state['values']['name'];
  $container['settings'] = $form_state['values']['settings'] + $container['settings'];

  $updated_container = _storage_service_invoke($container['service_id'], 'container_update', $container);
  $container = $updated_container ? $updated_container : $container;

  drupal_write_record('storage_container', $container, 'container_id');
  _storage_container_flush_serving_container($container['container_id']);

  $message = 'Storage container <i>' . $container['name'] . '</i> has been updated.';
  $path = 'admin/build/storage/containers/' . $container['container_id'];

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $path));

  $form_state['redirect'] = 'admin/build/storage/containers/' . $container['container_id'];
}

