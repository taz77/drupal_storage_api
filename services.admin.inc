<?php
// $Id$

// Copyright 2008-2009 Jonathan Brown


_storage_api_update_services();


function _storage_api_services_query() {

  $objects_subquery = db_select('storage_container');
  $objects_subquery->join('storage_object_instance', NULL, 'storage_container.container_id = storage_object_instance.container_id');
  $objects_subquery->join('storage_object', NULL, 'storage_object_instance.object_id = storage_object.object_id');
  
  $objects_subquery->fields('storage_container', array('service_id'));
  $objects_subquery->addExpression('COUNT(storage_object.object_id)', 'count');
  $objects_subquery->addExpression('SUM(storage_object.size)', 'size');
  
  $objects_subquery->groupBy('storage_container.service_id');


  $files_subquery = db_select('storage_container');
  $files_subquery->join('storage_file_instance', NULL, 'storage_container.container_id = storage_file_instance.container_id');
  $files_subquery->join('storage_file', NULL, 'storage_file_instance.file_id = storage_file.file_id');
  $files_subquery->join('storage_object', NULL, 'storage_file.object_id = storage_object.object_id');
  
  $files_subquery->fields('storage_container', array('service_id'));
  $files_subquery->addExpression('COUNT(storage_file.file_id)', 'count');
  $files_subquery->addExpression('SUM(storage_object.size)', 'size');
  
  $files_subquery->groupBy('storage_container.service_id');


  $query = db_select('storage_service', NULL, array('fetch' => PDO::FETCH_ASSOC));
  $query->leftJoin($objects_subquery, 'objects', 'storage_service.service_id = objects.service_id');
  $query->leftJoin($files_subquery, 'files', 'storage_service.service_id = files.service_id');
  
  $query->fields('storage_service');

  $query->addField('objects', 'count', 'objects_count');
  $query->addField('objects', 'size', 'objects_size');

  $query->addField('files', 'count', 'files_count');
  $query->addField('files', 'size', 'files_size');

  return $query;
}


function storage_api_services() {

  drupal_set_title(t('Storage services'), PASS_THROUGH);

  $header = array(t('Service'), t('Suspended'), t('Instances'), t('Size'));

  $result = _storage_api_services_query()
    ->orderBy('storage_service.service_id')
    ->execute();

  foreach($result as $service) {
    unset($row);
  
    $name = _storage_api_get_service_name($service['service_id']);
  
    $row[] = l($name, 'admin/build/storage/services/' . $service['service_id']);
    $row[] = $service['suspended'] ? t('Yes') : t('No');
    $row[] = $service['objects_count'] + $service['files_count'];
    $row[] = storage_api_format_size($service['objects_size'] + $service['files_size']);
    
    $rows[] = $row;
  }
  
  if(empty($rows)) {
    $rows[] = array(array('data' => t('No service modules enabled.'), 'colspan' => 4, 'class' => 'message'));
  }

  return theme('table', array(
    'header' => $header, 
    'rows' => $rows,
    'attributes' => array('style' => 'width: auto;'),
  ));
}


function _storage_api_service_flush_serve_url($service_id) {

  $selector_subquery = db_select('storage_container');
  $selector_subquery->join('storage_class_container', NULL, 'storage_container.container_id = storage_class_container.container_id');
  $selector_subquery->join('storage_selector', NULL, 'storage_class_container.class_id = storage_selector.class_id');
  
  $selector_subquery->fields('storage_selector', array('selector_id'))
    ->condition('storage_container.service_id', $service_id);
    
  db_update('storage_file')
    ->expression('serve_url', 'NULL')
    ->condition('selector_id', $selector_subquery, 'IN')
    ->execute();

  cache_clear_all();
}


/**
 * FAPI definition for the service form.
 *
 * @ingroup forms
 * @see storage_api_service_form_suspend()
 * @see storage_api_service_form_unsuspend()
 */
function storage_api_service_form($form_state, $service) {

  drupal_set_title(t('Storage service: ') . $service['name'], PASS_THROUGH);

  $rows[] = array(t('Suspended'), $service['suspended'] ? t('Yes') : t('No'));

  $info = _storage_api_services_query()
    ->condition('storage_service.service_id', $service['service_id'])
    ->execute()
    ->fetchAssoc();
  
  $rows[] = array(t('Instances'), $info['objects_count'] + $info['files_count']);
  $rows[] = array(t('Size'), storage_api_format_size($info['objects_size'] + $info['files_size']));
  
  foreach($rows as &$row) {
    $row[0] = array(
      'data' => $row[0] . ':',
      'style' => 'font-weight: bold',
    );
  }
  
  $table = theme('table', array(
    'rows' => $rows,
    'attributes' => array('style' => 'width: auto;'),
  ));

  $form['status'] = array(
    '#markup' => '<p>' . $table . '</p>'
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


/**
 * Submit handler (suspend) for the service form.
 *
 * @see storage_api_service_form()
 * @see storage_api_service_form_unsuspend()
 */
function storage_api_service_form_suspend($form, &$form_state) {
  $service = $form['#parameters'][2];

  $form_state['redirect'] = 'admin/build/storage/services/' . $service['service_id'] . '/suspend';
}


/**
 * Submit handler (unsuspend) for the service form.
 *
 * @see storage_api_service_form()
 * @see storage_api_service_form_suspend()
 */
function storage_api_service_form_unsuspend($form, &$form_state) {
  $service = $form['#parameters'][2];

  $form_state['redirect'] = 'admin/build/storage/services/' . $service['service_id'] . '/unsuspend';
}


/**
 * FAPI definition for the service suspend form.
 *
 * @ingroup forms
 * @see storage_api_service_suspend_form_submit()
 */
function storage_api_service_suspend_form($form_state, $service) {

  return confirm_form($form, 'Are you sure you want to suspend storage service <i>' . $service['name'] . '</i> ?', 
    'admin/build/storage/services/' . $service['service_id'], t(''));
}


/**
 * Submit handler for the service suspend form.
 *
 * @see storage_api_service_suspend_form()
 */
function storage_api_service_suspend_form_submit($form, &$form_state) {
  $service = $form['#parameters'][2];
  
  $update = array(
    'service_id' => $service['service_id'],
    'suspended' => TRUE,
  );
  
  drupal_write_record('storage_service', $update, 'service_id');
  _storage_api_service_flush_serve_url($service['service_id']);

  $message = 'Storage service <i>' . $service['name'] . '</i> has been suspended.';
  $path = 'admin/build/storage/services/' . $service['service_id']; 

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $path));

  $form_state['redirect'] = $path;
}


/**
 * FAPI definition for the service unsuspend form.
 *
 * @ingroup forms
 * @see storage_api_service_unsuspend_form_submit()
 */
function storage_api_service_unsuspend_form($form_state, $service) {

  return confirm_form($form, 'Are you sure you want to unsuspend storage service <i>' . $service['name'] . '</i> ?', 
    'admin/build/storage/services/' . $service['service_id'], t(''));
}


/**
 * Submit handler for the service unsuspend form.
 *
 * @see storage_api_service_unsuspend_form()
 */
function storage_api_service_unsuspend_form_submit($form, &$form_state) {
  $service = $form['#parameters'][2];
  
  $update = array(
    'service_id' => $service['service_id'],
    'suspended' => FALSE,
  );
  
  drupal_write_record('storage_service', $update, 'service_id');
  _storage_api_service_flush_serve_url($service['service_id']);

  $message = 'Storage service <i>' . $service['name'] . '</i> has been unsuspended.';
  $path = 'admin/build/storage/services/' . $service['service_id']; 

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $path));

  $form_state['redirect'] = $path;
}

