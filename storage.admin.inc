<?php

/**
 * @file
 * Admin UI functions for Storage API.
 */

/**
 * Helper function to return themed table of classes files and sizes. Returns
 * renderabled HTML.
 *
 * @return string
 */

function storage_admin_classes() {
  $header = [
    t('Class'),
    t('Files'),
    t('Size'),
  ];
  $rows = [];

  $class_ids = db_select('storage_class')
    ->fields('storage_class')
    ->orderBy('storage_class.name')
    ->orderBy('storage_class.class_id')
    ->execute()
    ->fetchCol();

  foreach ($class_ids as $class_id) {
    $class = storage_class_load($class_id);

    $rows[] = [
      $class->link(),
      $class->count(),
      $class->size(TRUE),
    ];
  }

  if (empty($rows)) {
    $rows[] = [
      [
        'data' => t('No classes configured.'),
        'colspan' => 3,
        'class' => 'message',
      ],
    ];
  }

  return theme('table', [
    'header' => $header,
    'rows' => $rows,
    'attributes' => ['style' => 'width: auto;'],
  ]);
}

/**
 * Helper function to returned themed table of containers. Returns renderable
 * HTML
 *
 * @return string
 */
function storage_admin_containers() {
  $header = [
    t('Container'),
    t('Access control'),
    t('Status'),
    t('Files'),
    t('Size'),
  ];
  $rows = [];

  $result = db_select('storage_container', NULL, ['fetch' => PDO::FETCH_ASSOC])
    ->fields('storage_container')
    ->orderBy('name')
    ->orderBy('container_id')
    ->execute();

  foreach ($result as $container) {
    $container = storage_container_new($container);

    $rows[] = [
      $container->link(),
      $container->access_control ? t('Yes') : t('No'),
      $container->status(),
      $container->count(),
      $container->size(TRUE),
    ];
  }

  if (empty($rows)) {
    $rows[] = [
      [
        'data' => t('No containers exist.'),
        'colspan' => 6,
        'class' => 'message',
      ],
    ];
  }

  return theme('table', [
    'header' => $header,
    'rows' => $rows,
    'attributes' => ['style' => 'width: auto;'],
  ]);
}

/**
 * Helper function to return tabled classes and tabled containers. Returns
 * rendabled HTML.
 * 
 * @return string
 */

function storage_admin() {
  return storage_admin_classes() . storage_admin_containers();
}
