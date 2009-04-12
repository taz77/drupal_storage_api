<?php
// $Id$

// Copyright 2008-2009 Jonathan Brown


_storage_api_update_services();


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

