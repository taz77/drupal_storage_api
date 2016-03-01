<?php

/**
 * @file
 * Admin UI functions for Audit submodule of Storage API
 */

/**
 * Reads storage audit data.
 *
 * @param \StorageContainerInterface $container
 *   The storage container.
 *
 * @return array
 *   An associative array of audit data.
 */
function _storage_audit_read(StorageContainerInterface $container) {
  $missing = $invalid = $extra = [];

  $result = db_select('storage_instance', NULL, ['fetch' => PDO::FETCH_ASSOC])
    ->fields('storage_instance')
    ->condition('storage_instance.container_id', $container->container_id)
    ->condition(db_or()
      ->isNull('storage_instance.file_id')
      ->condition('storage_instance.audit', STORAGE_AUDIT_STATUS_AUDITED, '<>')
    )
    ->execute();

  foreach ($result as $instance) {
    $link = $container->instanceLink($instance['reference']);

    switch ($instance['audit']) {

      case STORAGE_AUDIT_STATUS_MISSING:
        $missing[] = $link;
        break;

      case STORAGE_AUDIT_STATUS_AUDITED:
        $extra[] = $link;
        break;

      case STORAGE_AUDIT_STATUS_INVALID:
        $invalid[] = $link;
        break;
    }
  }

  return [
    'missing' => $missing,
    'invalid' => $invalid,
    'extra' => $extra,
  ];
}

/**
 * Form builder function for storage audit settings.
 *
 * @param array $form
 *   The form.
 * @param array $form_state
 *   The form state.
 * @param \StorageContainerInterface $container
 *   The storage container.
 *
 * @return array
 *   The form array.
 *
 * @throws \Exception
 */
function storage_audit_form(array $form, array &$form_state, StorageContainerInterface $container) {
  $form['#container'] = $container;
  $audited = _storage_audit_get_audited($container->container_id);


  $form['info'] = [
    '#prefix' => '<p>',
    '#markup' => t('It is important that the record of which files have instances in which containers is accurate. If instances have been lost, then the database needs to be updated to reflect this.'),
    '#suffix' => '</p>',
  ];

  $info = [
    t('Service') => check_plain($container->service()->name),
    t('Files') => $container->count(),
    t('Size') => $container->size(TRUE),
    t('Audited') => isset($audited) ? format_date($audited) : t('No'),
  ];

  $form['status'] = [
    '#markup' => theme('storage_info_table', ['info' => $info]),
  ];

  $form['audit'] = [
    '#type' => 'submit',
    '#value' => t('Audit'),
    '#weight' => 1,
  ];

  if (isset($audited)) {

    $results = _storage_audit_read($container);

    $fieldsets = [
      'missing' => 'Missing',
      'invalid' => 'Invalid',
      'extra' => 'Extra',
    ];

    foreach ($fieldsets as $name => $title) {

      $form[$name] = [
        '#type' => 'fieldset',
        '#title' => $title . ' (' . count($results[$name]) . ')',
        '#collapsible' => TRUE,
        '#collapsed' => TRUE,
      ];

      $form[$name]['list'] = [
        '#markup' => '<code>' . implode('<br />', $results[$name]) . '</code>',
      ];
    }

    if ((count($results['missing']) + count($results['invalid']) + count($results['extra'])) > 0) {
      $form['repair'] = [
        '#type' => 'submit',
        '#value' => t('Repair'),
        '#weight' => 2,
      ];
    }
  }

  return $form;
}

/**
 * Submit handler (audit) for the container form.
 *
 * @see storage_container_form()
 */
function storage_audit_form_submit(array $form, array &$form_state) {
  $container = $form['#container'];

  switch ($form_state['clicked_button']['#id']) {

    case 'edit-audit':
      $batch = [
        'operations' => [
          ['storage_audit_batch', [$container]],
        ],
        'title' => t('Auditing storage container %name', ['%name' => $container->name()]),
      ];
      break;

    case 'edit-repair':
      $batch = [
        'operations' => [
          ['storage_audit_repair_batch', [$container]],
        ],
        'title' => t('Repairing storage container %name', ['%name' => $container->name()]),
      ];
      break;
  }

  $batch['progress_message'] = '@estimate remaining.';

  batch_set($batch);
  $form_state['rebuild'] = TRUE;
}
