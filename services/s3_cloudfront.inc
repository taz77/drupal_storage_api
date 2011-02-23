<?php
// $Id$

// Copyright 2008-2010 Jonathan Brown


class StorageS3CloudFront extends StorageContainer implements StorageServiceInterface {

  private function signature($source) {

    return base64_encode(hash_hmac('sha1', utf8_encode($source), $this->settings['secret_access_key'], TRUE));
  }
  
  private function parseHeaders($source) {
    $headers = array();

    $source = explode("\r\n", trim($source));
    array_shift($source);  // Get rid of the HTTP result line.

    foreach ($source as $header) {
      list($header, $value) = explode(':', $header . ':', 3);
      $headers[$header] = trim($value);
    }
    
    return $headers;
  }
  
  private function request($op, $bucket = NULL, $object_name = NULL, Storage $storage = NULL, $source = NULL, $query = array()) {
    $result = array();
    $date = date(DATE_RFC1123, time());

    $components = array($op, '', '', $date);
    
    $headers[] = 'Date: ' . $date;

    $curl_options = array(
      CURLOPT_RETURNTRANSFER => TRUE,
      CURLOPT_AUTOREFERER => TRUE,
      CURLOPT_FOLLOWLOCATION => TRUE,
      CURLOPT_USERAGENT => 'Storage API (http://drupal.org/project/storage_api)',
    );

    switch ($op) {
    
      case 'PUT':
        $curl_options[CURLOPT_PUT] = TRUE;
        
        if ($storage) {

          if ($this->capability == STORAGE_API_SERVING) {
            $headers[] = 'x-amz-acl: public-read';
            $components[] = 'x-amz-acl:public-read';
          }
          
          if (!empty($source['copy_source'])) {
            $headers[] = 'x-amz-copy-source: ' . $source['copy_source'];
            $components[] = 'x-amz-copy-source:' . $source['copy_source'];
          }
        }

        if ($source['filepath']) {
          $fp = fopen($source['filepath'], 'r');
          
          $components[1] = base64_encode($storage->md5);
          $components[2] = $storage->mimetype;
          
          $headers = array_merge($headers, $storage->HTTPHeaders($this->acsc));
          
          $curl_options[CURLOPT_INFILE] = $fp;
          $curl_options[CURLOPT_INFILESIZE] = $storage->size;
        }
        elseif (!$object_name && $this->settings['location']) {

          $str = '<CreateBucketConfiguration><LocationConstraint>';
          $str .= $this->settings['location'];
          $str .= '</LocationConstraint></CreateBucketConfiguration>';
          
          $fp = fopen('data:text/plain,' . $str, 'r');
          $curl_options[CURLOPT_INFILE] = $fp;
          $curl_options[CURLOPT_INFILESIZE] = strlen($str);
          $headers[] = 'Expect:';
        }
        else {
          $curl_options[CURLOPT_INFILESIZE] = 0;
          $headers[] = 'Expect:';
        }
        
        break;
        
      case 'HEAD':
        $curl_options[CURLOPT_NOBODY] = TRUE;
        $curl_options[CURLOPT_HEADER] = TRUE;
        break;
        
      case 'GET':
        if ($object_name) {
          $result['filepath'] = tempnam(file_directory_temp(), '');
          $fp = fopen($result['filepath'], 'w');
          $curl_options[CURLOPT_FILE] = $fp;
        }
        
        break;
        
      case 'DELETE':
        $curl_options[CURLOPT_CUSTOMREQUEST] = 'DELETE';
        break;
    }
    
    if ($bucket)
      $object_name = '/' . $object_name;
    
    $components[] = '/' . $bucket . drupal_encode_path($object_name);
    
    $signature = $this->signature(implode("\n", $components));
    $headers[] = 'Authorization: AWS ' . $this->settings['access_key_id'] . ':' . $signature;

    $curl_options[CURLOPT_HTTPHEADER] = $headers;
    
    if ($bucket)  
      $bucket .= '.';

    $ch = curl_init(url('http://' . $bucket . 's3.amazonaws.com' . drupal_encode_path($object_name), 
      array('query' => $query)));
    curl_setopt_array($ch, $curl_options);
    $transfer = curl_exec($ch);
    $result += curl_getinfo($ch);
    curl_close($ch);
    
    if (isset($fp)) {
      fclose($fp);
    }
      
    if (isset($result['filepath']) && $result['http_code'] != 200) {
      @unlink($result['filepath']);
      unset($result['filepath']);
    }
    
    if (is_string($transfer) && $transfer != '') {
      $result['transfer'] = $transfer;
      
      
      if ($op == 'HEAD') {
        $result['headers'] = $this->parseHeaders($transfer);
      }
      else {
        $result['simplexml'] = @simplexml_load_string($transfer);

        if ($result['simplexml']) {
          if ($result['simplexml']->getName() == 'Error') {

            $msg = (string) $result['simplexml']->Message;

            watchdog('storage_s3', 'Amazon S3 error: @msg', array('@msg' => $msg), WATCHDOG_ERROR);

            if (user_access('administer storage'))
              drupal_set_message(t('Amazon S3 error: @msg', array('@msg' => $msg)), 'error');
          }
        }
      }
    }
    
    return $result;
  }
  
  private function url($s3_object) {
    $query = array();

    if ($this->settings['cloudfront'] && 
      $this->settings['cloudfront_status'] == 'Deployed')
    {
      $path = 'http://' . $this->settings['cloudfront_domain'] . '/' . $s3_object;
    }
    else {
      $path = 'http://' . $this->settings['bucket'] . '.s3.amazonaws.com/' . $s3_object;

      if ($this->capability == STORAGE_API_ACCESS_CONTROL) {
        $query['AWSAccessKeyId'] = $this->settings['access_key_id'];
        $query['Expires'] = REQUEST_TIME + $this->settings['url_lifetime'];
        
        $components = array(
          'GET',
          '',
          '',
          $query['Expires'],
          '/' . $this->settings['bucket'] . '/' . $s3_object
        );
        
        $query['Signature'] = $this->signature(implode("\n", $components));
      }
    }

    return url($path, array('query' => $query));
  }

  private function rtmp($s3_object) {

    if ($this->settings['cloudfront_streaming'] && 
      $this->settings['cloudfront_streaming_status'] == 'Deployed')
    {
      return array(
        'streamer' => 'rtmp://' . $this->settings['cloudfront_streaming_domain'] . '/cfx/st',
        'file' => $s3_object,
      );
    }

    throw new Exception();
  }

  private function requestCloudFront($op, $streaming = FALSE, $xml = NULL) {
    $result = array();
    $date = date(DATE_RFC1123, time());

    $headers[] = 'Date: ' . $date;

    $curl_options = array(
      CURLOPT_HEADER => TRUE,
      CURLOPT_RETURNTRANSFER => TRUE,
      CURLOPT_AUTOREFERER => TRUE,
      CURLOPT_FOLLOWLOCATION => TRUE,
      CURLOPT_USERAGENT => 'Storage API (http://drupal.org/project/storage_api)',
    );

    if ($streaming) {
      $path = '/2009-12-01/streaming-distribution';
      
      if (isset($this->settings['cloudfront_streaming_id'])) {
        $id = $this->settings['cloudfront_streaming_id'];
      }
    }
    else {
      $path = '/2009-12-01/distribution';

      if (isset($this->settings['streaming_id'])) {
        $id = $this->settings['streaming_id'];
      }
    }

    switch ($op) {
    
      case 'POST':
        $curl_options[CURLOPT_POST] = TRUE;
        $curl_options[CURLOPT_POSTFIELDS] = $xml;
        $headers[] = 'Content-Type: text/xml';
        $headers[] = 'Expect:';
        break;

      case 'PUT':
        $curl_options[CURLOPT_PUT] = TRUE;
        
        $fp = fopen('data:text/plain,' . $xml, 'r');
        $curl_options[CURLOPT_INFILE] = $fp;
        $curl_options[CURLOPT_INFILESIZE] = strlen($xml);
        $headers[] = 'Expect:';
        $headers[] = 'If-Match: ' . $this->settings['etag'];
        
        $path .= '/' . $id . '/config';
        break;
        
      case 'GET':
        $path .= '/' . $id;
        break;
        
      case 'DELETE':
        $curl_options[CURLOPT_CUSTOMREQUEST] = 'DELETE';
        $headers[] = 'If-Match: ' . $this->settings['etag'];
        $path .= '/' . $id;
        break;
    }
    
    $headers[] = 'Authorization: AWS ' . $this->settings['access_key_id'] . ':' . $this->signature($date);

    $curl_options[CURLOPT_HTTPHEADER] = $headers;
    
    $ch = curl_init('https://cloudfront.amazonaws.com' . $path);
    curl_setopt_array($ch, $curl_options);
    $transfer = curl_exec($ch);
    $result = curl_getinfo($ch);
    curl_close($ch);
    
    if (isset($fp)) {
      fclose($fp);
    }
      
    if (is_string($transfer) && $transfer != '') {
      
      list($headers, $result['body']) = explode("\r\n\r\n", $transfer, 2);
      
      $result['headers'] = $this->parseHeaders($headers); 
      
      $result['simplexml'] = @simplexml_load_string($result['body']);
      
      if ($result['simplexml']) {
        if ($result['simplexml']->getName() == 'ErrorResponse') {
        
          $msg = (string) $result['simplexml']->Error->Message;

          watchdog('storage_s3', 'Amazon CloudFront error: @msg', array('@msg' => $msg), WATCHDOG_ERROR);

          if (user_access('administer storage'))
            drupal_set_message(t('Amazon CloudFront error: @msg', array('@msg' => $msg)), 'error');
        }
      }
    }
    
    return $result;
  }

  private function locations() {

    return array(
      '' => t('US Standard'),
      'us-west-1' => t('US-West (Northern California)'),
      'EU' => t('EU (Ireland)'),
      'ap-southeast-1' => t('APAC (Singapore)'),
    );
  }

  private function callerReference() {
    static $id;

    // Make sure we can create multiple distributions at the same time.
    return REQUEST_TIME . '-' . ++$id;
  }

  private function distributionCreate() {

    $xml = '<?xml version="1.0" encoding="UTF-8"?>';
    $xml .= '<DistributionConfig xmlns="http://cloudfront.amazonaws.com/doc/2009-12-01/">';
    $xml .= '<Origin>' . $this->settings['bucket'] . '.s3.amazonaws.com</Origin>';
    $xml .= '<CallerReference>' . $this->callerReference() . '</CallerReference>';
    $xml .= '<Enabled>true</Enabled>';
    $xml .= '</DistributionConfig>';

    $result = $this->requestCloudFront('POST', FALSE, $xml);
    
    if ($result['http_code'] != 201) {
      $this->settings['cloudfront'] = FALSE;
      return;
    }

    $this->settings['cloudfront_id'] = (string) $result['simplexml']->Id;
    $this->settings['cloudfront_status'] = (string) $result['simplexml']->Status;
    $this->settings['cloudfront_domain'] = (string) $result['simplexml']->DomainName;

    $message = 'Added CloudFront distribution to storage container <i>' . $this->name() . '</i>.';

    drupal_set_message($message);
    watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $this->path()));
  }

  private function distributionCreateStreaming() {

    $xml = '<?xml version="1.0" encoding="UTF-8"?>';
    $xml .= '<StreamingDistributionConfig xmlns="http://cloudfront.amazonaws.com/doc/2009-12-01/">';
    $xml .= '<Origin>' . $this->settings['bucket'] . '.s3.amazonaws.com</Origin>';
    $xml .= '<CallerReference>' . $this->callerReference() . '</CallerReference>';
    $xml .= '<Enabled>true</Enabled>';
    $xml .= '</StreamingDistributionConfig>';

    $result = $this->requestCloudFront('POST', TRUE, $xml);
    
    if ($result['http_code'] != 201) {
      $this->settings['cloudfront_streaming'] = FALSE;
      return;
    }

    $this->settings['cloudfront_streaming_id'] = (string) $result['simplexml']->Id;
    $this->settings['cloudfront_streaming_status'] = (string) $result['simplexml']->Status;
    $this->settings['cloudfront_streaming_domain'] = (string) $result['simplexml']->DomainName;

    $message = 'Added CloudFront streaming distribution to storage container <i>' . $this->name() . '</i>.';

    drupal_set_message($message);
    watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $this->path()));
  }

  private function distributionDeploy() {

    $deploy = variable_get('storage_s3_cloudfront_deploy', array());
    
    foreach ($deploy as $container_id => $null) {
    
      $container = storage_container_load($container_id);
      
      $result = $this->requestCloudFront('GET');
      
      if (!$result['http_code'])
        continue;
      
      if ($result['http_code'] != 200) {
        $container->settings['cloudfront'] = FALSE;
        $message = 'Failed to deploy CloudFront distribution for storage container <i>' . $container->name() . '</i>.';
      }
      else {
        $container->settings['cloudfront_status'] = (string) $result['simplexml']->Status;
        
        if ($container->settings['cloudfront_status'] != 'Deployed')
          continue;

        $message = 'Deployed CloudFront distribution for storage container <i>' . $container->name() . '</i>.';
      }
    
      $container->update();
      $container->flushServingContainer();
      
      unset($deploy[$container_id]);
      variable_set('storage_s3_cloudfront_deploy', $deploy);

      watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $container->path()));
    }
  }

  private function distributionDeployStreaming() {

    $deploy = variable_get('storage_s3_cloudfront_streaming_deploy', array());
    
    foreach ($deploy as $container_id => $null) {
    
      $container = storage_container_load($container_id);
      
      $result = $this->requestCloudFront('GET', TRUE);
      
      if (!$result['http_code'])
        continue;

      if ($result['http_code'] != 200) {
        $container->settings['cloudfront_streaming'] = FALSE;
        $message = 'Failed to deploy CloudFront streaming distribution for storage container <i>' . 
          $container->name() . '</i>.';
      }
      else {
        $container->settings['cloudfront_streaming_status'] = (string) $result['simplexml']->Status;
        
        if ($container->settings['cloudfront_streaming_status'] != 'Deployed')
          continue;
    
        $message = 'Deployed CloudFront streaming distribution for storage container <i>' . $container->name() . '</i>.';
      }

      $container->update();
      $container->flushServingContainer();

      unset($deploy[$container_id]);
      variable_set('storage_s3_cloudfront_streaming_deploy', $deploy);
      
      watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $container->path()));
    }
  }

  private function distributionDelete() {

    $delete = variable_get('storage_s3_cloudfront_delete', array());
    
    foreach ($delete as $container) {
    
      $result = $this->requestCloudFront('GET');
      
      if (!$result['http_code'])
        continue;

      if ($result['http_code'] == 200) {
        
        $container->settings['etag'] = $result['headers']['ETag'];
          
        if ($result['simplexml']->DistributionConfig->Enabled == 'true') {
        
          $xml = '<?xml version="1.0" encoding="UTF-8"?>';
          $xml .= '<DistributionConfig xmlns="http://cloudfront.amazonaws.com/doc/2009-12-01/">';
          $xml .= '<Origin>' . $container->settings['bucket'] . '.s3.amazonaws.com</Origin>';
          $xml .= '<CallerReference>' . $result['simplexml']->DistributionConfig->CallerReference . '</CallerReference>';
          $xml .= '<Enabled>false</Enabled>';
          $xml .= '</DistributionConfig>';
        
          $result = $this->requestCloudFront('PUT', FALSE, $xml);

          if ($result['http_code'] == 200) {

            $message = 'Disabled CloudFront distribution for destroyed container <i>' . $container->name() . '</i>.';
            watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE);
            continue;
          }
        }
        else {
          
          if ($result['simplexml']->Status == 'InProgress')
            continue;
        
          $result = $this->requestCloudFront('DELETE');

          if ($result['http_code'] == 204) {

            $message = 'Deleted CloudFront distribution for destroyed container <i>' . $container->name() . '</i>.';
            watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE);
          }
        }
      }

      unset($delete[$container->container_id]);
      variable_set('storage_s3_cloudfront_delete', $delete);
    }
  }

  private function distributionDeleteStreaming() {

    $delete = variable_get('storage_s3_cloudfront_streaming_delete', array());
    
    foreach ($delete as $container) {
    
      $result = $this->requestCloudFront('GET', TRUE);
      
      if (!$result['http_code'])
        continue;

      if ($result['http_code'] == 200) {
        
        $container->settings['etag'] = $result['headers']['ETag'];
          
        if ($result['simplexml']->StreamingDistributionConfig->Enabled == 'true') {
        
          $xml = '<?xml version="1.0" encoding="UTF-8"?>';
          $xml .= '<StreamingDistributionConfig xmlns="http://cloudfront.amazonaws.com/doc/2009-12-01/">';
          $xml .= '<Origin>' . $container->settings['bucket'] . '.s3.amazonaws.com</Origin>';
          $xml .= '<CallerReference>' . $result['simplexml']->StreamingDistributionConfig->CallerReference . '</CallerReference>';
          $xml .= '<Enabled>false</Enabled>';
          $xml .= '</StreamingDistributionConfig>';
        
          $result = $this->requestCloudFront('PUT', TRUE, $xml);

          if ($result['http_code'] == 200) {

            $message = 'Disabled CloudFront streaming distribution for destroyed container <i>' . $container->name() . '</i>.';
            watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE);
            continue;
          }
        }
        else {
          
          if ($result['simplexml']->Status == 'InProgress')
            continue;
        
          $result = $this->requestCloudFront('DELETE', TRUE);

          if ($result['http_code'] == 204) {

            $message = 'Deleted CloudFront streaming distribution for destroyed container <i>' . $container->name() . '</i>.';
            watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE);
          }
        }
      }
          
      unset($delete[$container->container_id]);
      variable_set('storage_s3_cloudfront_streaming_delete', $delete);
    }
  }
  
  
  public function serviceSettingsForm($edit = FALSE) {

    $items['credentials_info'] = array(
      '#markup' => t('Get your credentials !link.', array('!link' => l(t('here'), 
        'http://aws-portal.amazon.com/gp/aws/developer/account/index.html?action=access-key', 
        array('attributes' => array('target' => '_blank')))))
    );

    $items['access_key_id'] = array(
      '#type' => 'textfield',
      '#title' => t('Access key ID'),
      '#required' => TRUE,
      '#validated' => TRUE,
      '#default_value' => isset($this->settings['access_key_id']) ? $this->settings['access_key_id'] : NULL,
    );
    
    $items['secret_access_key'] = array(
      '#type' => 'password',
      '#title' => t('Secret access key'),
      '#required' => TRUE,
      '#validated' => TRUE,
    );

    if (!$edit) {

      $items['bucket'] = array(
        '#type' => 'textfield',
        '#title' => t('Bucket name'),
        '#required' => TRUE,
        '#validated' => TRUE,
        '#default_value' => isset($this->settings['bucket']) ? $this->settings['bucket'] : NULL,
        '#maxlength' => 63,
        '#description' => t('The bucket namespace is shared by all users of Amazon S3.'),
      );

      $items['location'] = array(
        '#type' => 'select',
        '#title' => t('Location'),
        '#options' => $this->locations(),
        '#default_value' => isset($this->settings['location']) ? $this->settings['location'] : NULL,
      );
    }
    
    if ($this->capability == STORAGE_API_SERVING) {
    
  //    $description = t('(seconds)<br />This container will produce cryptographically signed URLs that will cease to function once their lifetime is over.');
    
      $items['cloudfront'] = array(
        '#type' => 'checkbox',
        '#title' => t('Serve with CloudFront'),
        '#default_value' => isset($this->settings['cloudfront']) ? $this->settings['cloudfront'] : NULL,
  //      '#description' => $description,
      );

      $items['cloudfront_streaming'] = array(
        '#type' => 'checkbox',
        '#title' => t('Stream with CloudFront'),
        '#default_value' => isset($this->settings['cloudfront_streaming']) ? $this->settings['cloudfront_streaming'] : NULL,
  //      '#description' => $description,
      );
    }

    if ($this->capability == STORAGE_API_ACCESS_CONTROL) {
    
      $description = t('(seconds)<br />This container will produce cryptographically signed URLs that will cease to function once their lifetime is over.');
    
      $items['url_lifetime'] = array(
        '#type' => 'textfield',
        '#title' => t('URL lifetime'),
        '#required' => TRUE,
        '#validated' => TRUE,
        '#default_value' => isset($this->settings['url_lifetime']) ? $this->settings['url_lifetime'] : 60 * 60,
        '#description' => $description,
        '#size' => 8,
        '#maxlength' => 8,
      );
    }

    return $items;
  }
  
  public function serviceSettingsValidate($edit = FALSE) {

    if (!$this->settings['access_key_id']) {
      form_set_error('settings][access_key_id', t('Access Key ID field is required.'));
    }

    if (!$this->settings['secret_access_key']) {
      form_set_error('settings][secret_access_key', t('Secret Access Key field is required.'));
    }
    
    if (!$this->settings['bucket']) {
      form_set_error('settings][bucket', t('Bucket name field is required.'));
    }
    
    if (form_get_errors())
      return;
      
    
    // Test account credentials.
    $result = $this->request('GET');   // HEAD doesn't work
    
    if ($result['http_code'] != 200) {
      form_set_error('settings][bucket');
      return;
    }
  }
  
  public function serviceCreate() {

    // Check if bucket is already owned by this account.
    $result = $this->request('HEAD', $this->settings['bucket']);

    if ($result['http_code'] == 200) {
    
      $result = $this->request('GET', $this->settings['bucket']);
      
      if (count($result['simplexml']->Contents) > 0) {
      
        form_set_error('settings][bucket', t('Bucket %name is already owned by this account, but is not empty.', 
          array('%name' => $this->settings['bucket'])));
          
        throw new Exception();
      }
    }
    else {
      $result = $this->request('PUT', $this->settings['bucket']);

      if ($result['http_code'] != 200) {
        throw new Exception();
      }

      $message = 'Acquired S3 bucket <i>' . $this->settings['bucket'] . '</i>.';
      drupal_set_message($message);
      watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE);
    }
  }
  
  public function servicePostCreate() {

    if ($this->settings['cloudfront']) {
      $this->distributionCreate();
      
      $deploy = variable_get('storage_s3_cloudfront_deploy', array());
      $deploy[$this->container_id] = TRUE;
      variable_set('storage_s3_cloudfront_deploy', $deploy);
    }

    if ($this->settings['cloudfront_streaming']) {
      $this->distributionCreateStreaming();
      
      $deploy = variable_get('storage_s3_cloudfront_streaming_deploy', array());
      $deploy[$this->container_id] = TRUE;
      variable_set('storage_s3_cloudfront_streaming_deploy', $deploy);
    }
  }

  public function serviceUpdate() {

    if ($this->settings['cloudfront'] && 
      !isset($this->settings['cloudfront_id']))
    {
      $this->distributionCreate();
      
      if ($this->settings['cloudfront']) {

        $deploy = variable_get('storage_s3_cloudfront_deploy', array());
        $deploy[$this->container_id] = TRUE;
        variable_set('storage_s3_cloudfront_deploy', $deploy);
      }
    }
    
    if ($this->settings['cloudfront_streaming'] && 
      !isset($this->settings['cloudfront_streaming_id']))
    {
      $this->distributionCreateStreaming();
      
      if ($this->settings['cloudfront_streaming']) {

        $deploy = variable_get('storage_s3_cloudfront_streaming_deploy', array());
        $deploy[$this->container_id] = TRUE;
        variable_set('storage_s3_cloudfront_streaming_deploy', $deploy);
      }
    }
  }

  public function serviceInfo() {

    $locations = $this->locations();

    $info = array(
      t('Bucket') => $this->settings['bucket'],
      t('Location') => $locations[$this->settings['location']],
    );
    
    if ($this->capability == STORAGE_API_SERVING) {
    
      if ($this->settings['cloudfront']) {
      
        if ($this->settings['cloudfront_status'] == 'Deployed')
          $status = t('Yes');
        else
          $status = t('Setting up. This can take a few minutes.');
      }
      else {
        $status = t('No');
      }
    
      $info[t('Serving with CloudFront')] = $status;
      
      if (isset($this->settings['cloudfront_status']) && 
        $this->settings['cloudfront_status'] == 'Deployed')
      {
        $info[t('CloudFront domain name')] = $this->settings['cloudfront_domain'];
      }

      if ($this->settings['cloudfront_streaming']) {
      
        if ($this->settings['cloudfront_streaming_status'] == 'Deployed')
          $status = t('Yes');
        else
          $status = t('Setting up. This can take a few minutes.');
      }
      else {
        $status = t('No');
      }
    
      $info[t('Streaming with CloudFront')] = $status;
      
      if (isset($this->settings['cloudfront_streaming_status']) &&
        $this->settings['cloudfront_streaming_status'] == 'Deployed')
      {
        $info[t('CloudFront streaming domain name')] = $this->settings['cloudfront_streaming_domain'];
      }
    }
    
    if ($this->capability == STORAGE_API_ACCESS_CONTROL)
      $info[t('URL lifetime')] = format_interval($this->settings['url_lifetime']);

    return $info;
  }
  
  public function serviceDestroy() {

    if (isset($this->settings['cloudfront_id'])) {

      $delete = variable_get('storage_s3_cloudfront_delete', array());
      $delete[$this->container_id] = $this;
      variable_set('storage_s3_cloudfront_delete', $delete);
    }

    if (isset($this->settings['cloudfront_streaming_id'])) {

      $delete = variable_get('storage_s3_cloudfront_streaming_delete', array());
      $delete[$this->container_id] = $this;
      variable_set('storage_s3_cloudfront_streaming_delete', $delete);
    }
  }
  
  public function serviceMaintenance() {
    $this->distributionDeploy();
    $this->distributionDeployStreaming();
    $this->distributionDelete();
    $this->distributionDeleteStreaming();
  }

  public function serviceInstanceCreate(Storage $storage, array $source) {
    
    if ($this->acsc) {
      $reference = $storage->file_id . '/' . $storage->filename;
    }
    else {
      $reference = $this->uniqueFilename($storage->filename);
    }

    $result = $this->request('PUT', $this->settings['bucket'], $reference, $storage, $source);
    
    if ($result['http_code'] == 200) {
      return $reference;
    }
    else {
      throw new Exception();
    }
  }

  public function serviceInstanceCopy(Storage $storage, $reference, $source_container) {
    
    $source['copy_source'] = '/' . $source_container['settings']['bucket'] . '/file-' . $storage->file_id;
    
    $result = $this->request('PUT', $this->settings['bucket'], $reference, $source);
    
    if ($result['http_code'] == 200 && $result['simplexml']) {

      if ($result['simplexml']->getName() == 'CopyObjectResult')
        return TRUE;
    }
    
    return FALSE;
  }
  
  private function eTag2md5($etag) {
    return pack('H*', substr($etag, 1, 32));
  }

  public function serviceManifest(&$sandbox, &$finished) {

    $query = array(
      'max-keys' => 100,
    );
    
    if ($sandbox['marker']) {
      $query['marker'] = $sandbox['marker'];
    }
  
    $result = $this->request('GET', $this->settings['bucket'], NULL, NULL, NULL, $query);
    
    foreach ($result['simplexml']->Contents as $object) {
    
      $manifest[] = array(
        'reference' => (string) $object->Key,
        'size' => (string) $object->Size,
        'md5' => $this->eTag2md5($object->ETag),
      );
    }
    
    $sandbox['marker'] = (string) $object->Key;
    $finished = $result['simplexml']->IsTruncated == 'false';
    
    return $manifest;
  }

  public function serviceInstanceExists($reference) {

    $result = $this->request('HEAD', $this->settings['bucket'], $reference);
    
    return $result['http_code'] == 200;
  }

  public function serviceInstanceCheck($reference) {

    $result = $this->request('HEAD', $this->settings['bucket'], $reference);
    
    if ($result['http_code'] != 200) {
      return FALSE;
    }
    
    return array(
      'size' => $result['headers']['Content-Length'],
      'md5' => $this->eTag2md5($result['headers']['ETag']),
    );
  }
  
  public function serviceInstanceDestroy($reference) {
    
    $result = $this->request('DELETE', $this->settings['bucket'], $reference);
    
    if ($result['http_code'] != 204) {
      throw new Exception();
    }
  }
  
  public function serviceInstanceGetFilepath($reference) {
    
    $result = $this->request('GET', $this->settings['bucket'], $reference);

    return $result['filepath'];
  }

  public function serviceInstanceServe($reference) {

    return $this->url($reference);
  }
}

/**
 * Implement hook_storage_instance_serve_rtmp() .
 */
function storage_s3_cloudfront_storage_instance_serve_rtmp(Storage $storage, $reference) {

  return _storage_s3_cloudfront_rtmp($this, $storage['file_id']);
}

