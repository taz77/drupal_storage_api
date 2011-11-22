<?php

// Copyright 2008-2011 Jonathan Brown


class StorageS3CloudFront extends StorageContainer implements StorageContainerInterface {

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
  
  private function request($op, $bucket = NULL, $object_name = NULL, Storage $storage = NULL, $uri = NULL, array $query = array()) {
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

          if (!$this->access_control) {
            $headers[] = 'x-amz-acl: public-read';
            $components[] = 'x-amz-acl:public-read';
          }

          if (!empty($this->settings['reduced_redundancy'])) {
            $headers[] = 'x-amz-storage-class: REDUCED_REDUNDANCY';
            $components[] = 'x-amz-storage-class:REDUCED_REDUNDANCY';
          }

          if (isset($storage->copy_source)) {
            $headers[] = 'x-amz-copy-source: ' . $storage->copy_source;
            $components[] = 'x-amz-copy-source:' . $storage->copy_source;
          }
        }

        if ($uri) {
          $fp = fopen($uri, 'r');
          
          $components[1] = base64_encode($storage->md5);
          $components[2] = $storage->mimetype;
          
          $headers = array_merge($headers, $storage->HTTPHeaders($this->enhanced_caching));
          
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
          $result['temp_uri'] = storage_temp_uri();
          $fp = fopen($result['temp_uri'], 'w');
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
      
    if (isset($result['temp_uri']) && $result['http_code'] != 200) {
      unset($result['temp_uri']);
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

    if ($this->settings['cloudfront'] && $this->settings['cloudfront_status'] == 'Deployed') {
      $path = 'http://' . $this->settings['cloudfront_domain'] . '/' . $s3_object;
    }
    else {
      $path = 'http://' . $this->settings['bucket'] . '.s3.amazonaws.com/' . $s3_object;

      if ($this->access_control) {
        $query['AWSAccessKeyId'] = $this->settings['access_key_id'];
        $query['Expires'] = REQUEST_TIME + $this->settings['url_lifetime'];
        
        $components = array(
          'GET',
          '',
          '',
          $query['Expires'],
          '/' . $this->settings['bucket'] . '/' . $s3_object,
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

    throw new StorageException();
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

      if (isset($this->settings['cloudfront_id'])) {
        $id = $this->settings['cloudfront_id'];
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
      'us-west-2' => t('US-West (Oregon)'),
      'EU' => t('EU (Ireland)'),
      'ap-southeast-1' => t('Asia Pacific (Singapore)'),
      'ap-northeast-1' => t('Asia Pacific (Tokyo)'),
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
    $result = $this->requestCloudFront('GET');
    
    // If we can't get through to AWS then abort.
    if (!$result['http_code']) {
      return;
    }
    
    if ($result['http_code'] != 200) {
      $this->settings['cloudfront'] = FALSE;
      $message = 'Failed to deploy CloudFront distribution for storage container <i>' . $this->name() . '</i>.';
    }
    else {
      $this->settings['cloudfront_status'] = (string) $result['simplexml']->Status;
      
      if ($this->settings['cloudfront_status'] != 'Deployed') {
        return;
      }

      $message = 'Deployed CloudFront distribution for storage container <i>' . $this->name() . '</i>.';
    }

    $this->update();
    $this->flushServingContainer();

    watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $this->path()));
  }

  private function distributionDeployStreaming() {
    $result = $this->requestCloudFront('GET', TRUE);
    
    // If we can't get through to AWS then abort.
    if (!$result['http_code']) {
      return;
    }
    
    if ($result['http_code'] != 200) {
      $this->settings['cloudfront_streaming'] = FALSE;
      $message = 'Failed to deploy CloudFront streaming distribution for storage container <i>' . $this->name() . '</i>.';
    }
    else {
      $this->settings['cloudfront_streaming_status'] = (string) $result['simplexml']->Status;
      
      if ($this->settings['cloudfront_streaming_status'] != 'Deployed') {
        return;
      }

      $message = 'Deployed CloudFront streaming distribution for storage container <i>' . $this->name() . '</i>.';
    }

    $this->update();
    $this->flushServingContainer();

    watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $this->path()));
  }

  private function distributionDelete() {
    $result = $this->requestCloudFront('GET');
    
    // If we can't get through to AWS then abort.
    if (!$result['http_code']) {
      throw new StorageException();
    }

    // If the distribution doesn't exist, then there is nothing to do.
    if ($result['http_code'] != 200) {
      return;
    }

    $this->settings['etag'] = $result['headers']['ETag'];

    if ($result['simplexml']->DistributionConfig->Enabled == 'true') {

      $xml = '<?xml version="1.0" encoding="UTF-8"?>';
      $xml .= '<DistributionConfig xmlns="http://cloudfront.amazonaws.com/doc/2009-12-01/">';
      $xml .= '<Origin>' . $this->settings['bucket'] . '.s3.amazonaws.com</Origin>';
      $xml .= '<CallerReference>' . $result['simplexml']->DistributionConfig->CallerReference . '</CallerReference>';
      $xml .= '<Enabled>false</Enabled>';
      $xml .= '</DistributionConfig>';

      $result = $this->requestCloudFront('PUT', FALSE, $xml);

      if ($result['http_code'] == 200) {
        $message = 'Disabled CloudFront distribution for container <i>' . $this->name() . '</i>.';
        watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE);
      }
    }
    elseif ($result['simplexml']->Status != 'InProgress') {
      $result = $this->requestCloudFront('DELETE');

      if ($result['http_code'] == 204) {
        $message = 'Deleted CloudFront distribution for container <i>' . $this->name() . '</i>.';
        watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE);
        return;
      }
    }

    throw new StorageException();
  }

  private function distributionDeleteStreaming() {
    $result = $this->requestCloudFront('GET', TRUE);
    
    // If we can't get through to AWS then abort.
    if (!$result['http_code']) {
      throw new StorageException();
    }

    // If the distribution doesn't exist, then there is nothing to do.
    if ($result['http_code'] != 200) {
      return;
    }

    $this->settings['etag'] = $result['headers']['ETag'];

    if ($result['simplexml']->StreamingDistributionConfig->Enabled == 'true') {

      $xml = '<?xml version="1.0" encoding="UTF-8"?>';
      $xml .= '<StreamingDistributionConfig xmlns="http://cloudfront.amazonaws.com/doc/2009-12-01/">';
      $xml .= '<Origin>' . $this->settings['bucket'] . '.s3.amazonaws.com</Origin>';
      $xml .= '<CallerReference>' . $result['simplexml']->StreamingDistributionConfig->CallerReference . '</CallerReference>';
      $xml .= '<Enabled>false</Enabled>';
      $xml .= '</StreamingDistributionConfig>';

      $result = $this->requestCloudFront('PUT', TRUE, $xml);

      if ($result['http_code'] == 200) {
        $message = 'Disabled CloudFront streaming distribution for container <i>' . $this->name() . '</i>.';
        watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE);
      }
    }
    elseif ($result['simplexml']->Status != 'InProgress') {
      $result = $this->requestCloudFront('DELETE', TRUE);

      if ($result['http_code'] == 204) {
        $message = 'Deleted CloudFront streaming distribution for container <i>' . $this->name() . '</i>.';
        watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE);
        return;
      }
    }

    throw new StorageException();
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

      $items['reduced_redundancy'] = array(
        '#type' => 'checkbox',
        '#title' => t('Reduced redundancy'),
        '#default_value' => isset($this->settings['reduced_redundancy']) ? $this->settings['reduced_redundancy'] : NULL,
      );
    }

    if ($this->access_control) {
    
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
    else {
    
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
    
    if (form_get_errors()) {
      return;
    }
      
    
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
          
        throw new StorageException();
      }
    }
    else {
      $result = $this->request('PUT', $this->settings['bucket']);

      if ($result['http_code'] != 200) {
        throw new StorageException();
      }

      $message = 'Acquired S3 bucket <i>' . $this->settings['bucket'] . '</i>.';
      drupal_set_message($message);
      watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE);
    }
  }
  
  public function servicePostCreate() {

    if ($this->settings['cloudfront']) {
      $this->distributionCreate();
      $update = TRUE;
    }

    if ($this->settings['cloudfront_streaming']) {
      $this->distributionCreateStreaming();
      $update = TRUE;
    }

    return isset($update);
  }

  public function serviceUpdate() {

    if ($this->settings['cloudfront'] && !isset($this->settings['cloudfront_id'])) {
      $this->distributionCreate();
    }
    
    if ($this->settings['cloudfront_streaming'] && !isset($this->settings['cloudfront_streaming_id'])) {
      $this->distributionCreateStreaming();
    }
  }

  public function serviceInfo() {

    $locations = $this->locations();

    $info = array(
      t('Bucket') => $this->settings['bucket'],
      t('Location') => $locations[$this->settings['location']],
      t('Reduced redundancy') => $this->settings['reduced_redundancy'] ? t('Yes') : t('No'),
    );
    
    if ($this->access_control) {
      $info[t('URL lifetime')] = format_interval($this->settings['url_lifetime']);
    }
    else {

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

    return $info;
  }

  public function serviceDestroy() {
    $failed = FALSE;

    if (isset($this->settings['cloudfront_id'])) {
      try {
        $this->distributionDelete();
      }
      catch (StorageException $e) {
        $failed = TRUE;
      }
    }

    if (isset($this->settings['cloudfront_streaming_id'])) {
      try {
        $this->distributionDeleteStreaming();
      }
      catch (StorageException $e) {
        $failed = TRUE;
      }
    }

    if ($failed) {
      $this->update();
      throw new StorageException();
    }
  }
  
  public function serviceMaintenance() {

    if ($this->settings['cloudfront'] && $this->settings['cloudfront_status'] != 'Deployed') {
      $this->distributionDeploy();
    }

    if ($this->settings['cloudfront_streaming'] && $this->settings['cloudfront_streaming_status'] != 'Deployed') {
      $this->distributionDeployStreaming();
    }
  }

  public function serviceInstanceCreate(Storage $storage, $uri) {
    
    if ($this->enhanced_caching) {
      $reference = $storage->file_id . '/' . $storage->filename;
    }
    else {
      $reference = $this->uniqueFilename($storage->filename);
    }

    $result = $this->request('PUT', $this->settings['bucket'], $reference, $storage, $uri);
    
    if ($result['http_code'] == 200) {
      return $reference;
    }
    else {
      throw new StorageException();
    }
  }

  public function serviceInstanceCopy(Storage $storage, StorageContainer $source_container) {
    
    if ($this->enhanced_caching) {
      $reference = $storage->file_id . '/' . $storage->filename;
    }
    else {
      $reference = $this->uniqueFilename($storage->filename);
    }

    $storage->copy_source = '/' . $source_container->settings['bucket'] . '/' . $source_container->reference;
    
    $result = $this->request('PUT', $this->settings['bucket'], $reference, $storage);
    
    if ($result['http_code'] == 200 && $result['simplexml']) {

      if ($result['simplexml']->getName() == 'CopyObjectResult')
        return $reference;
    }
    
    throw new StorageException();
  }
  
  private function eTag2md5($etag) {
    return pack('H*', substr($etag, 1, 32));
  }

  public function serviceManifest(array &$sandbox, &$finished) {

    $query = array(
      'max-keys' => 100,
    );
    
    if ($sandbox['marker']) {
      $query['marker'] = $sandbox['marker'];
    }
  
    $result = $this->request('GET', $this->settings['bucket'], NULL, NULL, NULL, $query);
    
    foreach ($result['simplexml']->Contents as $object) {
    
      $manifest[(string) $object->Key] = array(
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
      throw new StorageException();
    }
    
    return array(
      'size' => $result['headers']['Content-Length'],
      'md5' => $this->eTag2md5($result['headers']['ETag']),
    );
  }
  
  public function serviceInstanceGetUri($reference) {
    
    $result = $this->request('GET', $this->settings['bucket'], $reference);

    return $result['temp_uri'];
  }

  public function serviceInstanceDestroy($reference) {
    
    $result = $this->request('DELETE', $this->settings['bucket'], $reference);
    
    if ($result['http_code'] != 204) {
      throw new StorageException();
    }
  }
  
  public function serviceInstanceServe($reference) {

    return $this->url($reference);
  }
}

