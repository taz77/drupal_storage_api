<?php
// $Id$

// Copyright 2008-2010 Jonathan Brown


_storage_update_services();


function _storage_services_query() {

  $subquery = db_select('storage_container');
  $subquery->join('storage_instance', NULL, 'storage_container.container_id = storage_instance.container_id');
  $subquery->join('storage', NULL, 'storage_instance.file_id = storage.file_id');
  $subquery->join('storage_file', NULL, 'storage.file_id = storage_file.file_id');
  
  $subquery->fields('storage_container', array('service_id'));
  $subquery->addExpression('COUNT(storage.storage_id)', 'count');
  $subquery->addExpression('SUM(storage_file.size)', 'size');
  
  $subquery->groupBy('storage_container.service_id');


  $query = db_select('storage_service', NULL, array('fetch' => PDO::FETCH_ASSOC));
  $query->leftJoin($subquery, 'files', 'storage_service.service_id = files.service_id');
  
  $query->fields('storage_service');

  $query->addField('files', 'count', 'count');
  $query->addField('files', 'size', 'size');

  return $query;
}


function storage_services() {

  drupal_set_title(t('Storage services'));

  $header = array(t('Service'), t('Suspended'), t('Instances'), t('Size'));

  $result = _storage_services_query()
    ->execute();

  foreach ($result as $fields) {
    $service = new StorageService($fields);
    unset($row);
  
    $row[] = $service->link();
    $row[] = $service->suspended ? t('Yes') : t('No');
    $row[] = (int)$service->count;
    $row[] = storage_format_size($service->size);
    
    $rows[$service->name()] = $row;
  }
  
  if (empty($rows)) {
    $rows[] = array(array('data' => t('No service modules enabled.'), 'colspan' => 4, 'class' => 'message'));
  }
  
  ksort($rows);

  return theme('table', array(
    'header' => $header, 
    'rows' => $rows,
    'attributes' => array('style' => 'width: auto;'),
  ));
}


/**
 * FAPI definition for the service form.
 *
 * @ingroup forms
 * @see storage_service_form_suspend()
 * @see storage_service_form_unsuspend()
 */
function storage_service_form($form, &$form_state, StorageService $service) {
  $form['#service'] = $service;


  $data = _storage_services_query()
    ->condition('storage_service.service_id', $service->service_id)
    ->execute()
    ->fetchAssoc();

  $info[t('Instances')] = (int)$data['count'];
  $info[t('Size')] = storage_format_size($data['size']);
  $info[t('Suspended')] = $service->suspended ? t('Yes') : t('No');

  $form['status'] = array(
    '#markup' => theme('storage_info_table', array('info' => $info))
  );

  if (!$service->suspended) {

    $form['suspend'] = array(
      '#type' => 'submit',
      '#value' => t('Suspend'),
      '#submit' => array('storage_service_form_suspend'),
    );
  }
  else {
    $form['unsuspend'] = array(
      '#type' => 'submit',
      '#value' => t('Unsuspend'),
      '#submit' => array('storage_service_form_unsuspend'),
    );
  }
  
  return $form;
}


/**
 * Submit handler (suspend) for the service form.
 *
 * @see storage_service_form()
 * @see storage_service_form_unsuspend()
 */
function storage_service_form_suspend($form, &$form_state) {
  $service = $form['#service'];

  $form_state['redirect'] = $service->path() . '/suspend';
}


/**
 * Submit handler (unsuspend) for the service form.
 *
 * @see storage_service_form()
 * @see storage_service_form_suspend()
 */
function storage_service_form_unsuspend($form, &$form_state) {
  $service = $form['#service'];

  $form_state['redirect'] = $service->path() . '/unsuspend';
}


/**
 * FAPI definition for the service suspend form.
 *
 * @ingroup forms
 * @see storage_service_suspend_form_submit()
 */
function storage_service_suspend_form($form, &$form_state, StorageService $service) {
  $form['#service'] = $service;

  return confirm_form($form, 'Are you sure you want to suspend storage service <i>' . $service->name() . '</i> ?', 
    $service->path(), t(''));
}


/**
 * Submit handler for the service suspend form.
 *
 * @see storage_service_suspend_form()
 */
function storage_service_suspend_form_submit($form, &$form_state) {
  $service = $form['#service'];
  
  $service->suspended = TRUE;
  $service->update();
  $service->flushServingContainer();

  $message = 'Storage service <i>' . $service->name() . '</i> has been suspended.';

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $service->path()));

  $form_state['redirect'] = $service->path();
}


/**
 * FAPI definition for the service unsuspend form.
 *
 * @ingroup forms
 * @see storage_service_unsuspend_form_submit()
 */
function storage_service_unsuspend_form($form, &$form_state, StorageService $service) {
  $form['#service'] = $service;

  return confirm_form($form, 'Are you sure you want to unsuspend storage service <i>' . $service->name() . '</i> ?', 
    $service->path(), t(''));
}


/**
 * Submit handler for the service unsuspend form.
 *
 * @see storage_service_unsuspend_form()
 */
function storage_service_unsuspend_form_submit($form, &$form_state) {
  $service = $form['#service'];
  
  $service->suspended = FALSE;
  $service->update();
  $service->flushServingContainer();

  $message = 'Storage service <i>' . $service->name() . '</i> has been unsuspended.';

  drupal_set_message($message);
  watchdog('storage_api', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $service->path()));

  $form_state['redirect'] = $service->path();
}
