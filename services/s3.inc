<?php

/**
 * @file
 * Amazon S3 Storage class files for Storage API.
 */

/**
 * Provides an Amazon S3 storage container.
 */
class StorageS3 extends StorageContainer implements StorageContainerInterface {

  /**
   * Generates an S3 request signature.
   *
   * @param string $source
   *   The source.
   * @param bool $cf
   *   Use CloudFront.
   *
   * @return bool|string
   *   The signature, or FALSE if the signature could not be generated.
   */
  private function signature($source, $cf = FALSE) {
    if (!$cf) {
      // @todo Determine why the secret key will not be pulled from settings
      return base64_encode(hash_hmac('sha1', utf8_encode($source), $this->settings['secret_access_key'], TRUE));
    }
    else {
      $key = openssl_get_privatekey($this->settings['cf_key_contents']);
      if (!$key) {
        watchdog('storage_s3', 'Amazon S3 error: Unable to get key from Cloudfront key file contents ID @id', ['@id' => $this->settings['cf_key_id']], WATCHDOG_ERROR);
        if (user_access('administer storage')) {
          drupal_set_message(t('Amazon S3 error: Unable to get key from Cloudfront key file contents ID @id', ['@id' => $this->settings['cf_key_id']]), 'error');
        }
        return FALSE;
      }
      if (!openssl_sign($source, $signed_source, $key)) {
        watchdog('storage_s3', 'Amazon S3 error: Unable to create signature using Cloudfront key file contents ID @id', ['@id' => $this->settings['cf_key_id']], WATCHDOG_ERROR);
        if (user_access('administer storage')) {
          drupal_set_message(t('Amazon S3 error: Unable to create signature using Cloudfront key file contents ID @id', ['@id' => $this->settings['cf_key_id']]), 'error');
        }
        return FALSE;
      }
      openssl_free_key($key);
      return str_replace(['+', '=', '/'], [
        '-',
        '_',
        '~',
      ], base64_encode($signed_source));
    }
  }

  /**
   * Parse a header into an array.
   */
  private function parseHeaders($source) {
    $headers = [];

    $source = explode("\r\n", trim($source));
    // Get rid of the HTTP result line.
    array_shift($source);

    foreach ($source as $header) {
      list($header, $value) = explode(':', $header . ':', 3);
      $headers[$header] = trim($value);
    }

    return $headers;
  }

  /**
   * Function to perform a request against S3.
   *
   * @param string $op
   *   The operation. Valid values are GET, PUT, HEAD, DELETE
   * @param string $bucket
   *   (optional) The bucket name.
   * @param string $object_name
   *   (optional) The object name.
   * @param Storage $storage
   *   (optional) The storage source.
   * @param string $uri
   *   (optional) The uri.
   * @param array $query
   *   (optional) An array of query string parameters.
   *
   * @return array
   *   The request result.
   */
  private function request($op, $bucket = NULL, $object_name = NULL, Storage $storage = NULL, $uri = NULL, array $query = []) {
    // @todo Convert to use Guzzle 6.x
    $result = [];
    $date = date(DATE_RFC1123, time());

    $components = [$op, '', '', $date];

    $headers[] = 'Date: ' . $date;

    $curl_options = [
      CURLOPT_RETURNTRANSFER => TRUE,
      CURLOPT_AUTOREFERER => TRUE,
      CURLOPT_FOLLOWLOCATION => TRUE,
      CURLOPT_USERAGENT => 'Storage API (https://www.drupal.org/project/storage_api)',
    ];

    // Apply the proxy details if we are utilising one.
    $proxy_server = variable_get('proxy_server');
    if (!empty($proxy_server)) {
      $proxy_curl_opts = [
        CURLOPT_PROXY => variable_get('proxy_server'),
        CURLOPT_PROXYUSERPWD => variable_get('proxy_username') . ':' . variable_get('proxy_password'),
        CURLOPT_PROXYPORT => variable_get('proxy_port'),
      ];

      $curl_options += $proxy_curl_opts;
    }

    switch ($op) {

      case 'PUT':
        $curl_options[CURLOPT_PUT] = TRUE;

        if ($storage) {

          if (!$this->access_control) {
            $headers[] = 'x-amz-acl: public-read';
            $components[] = 'x-amz-acl:public-read';
          }

          if (isset($storage->copy_source)) {
            $headers[] = 'x-amz-copy-source: ' . $storage->copy_source;
            $components[] = 'x-amz-copy-source:' . $storage->copy_source;
          }

          if (!empty($this->settings['reduced_redundancy'])) {
            $headers[] = 'x-amz-storage-class: REDUCED_REDUNDANCY';
            $components[] = 'x-amz-storage-class:REDUCED_REDUNDANCY';
          }

          $headers[] = 'Cache-Control: max-age=' . $this->settings['cache_control_max_age'];

        }

        if ($uri) {
          $fp = fopen($uri, 'r');
          if ($fp === FALSE) {
            throw new StorageException("Could not open source file");
          }

          $components[1] = base64_encode($storage->md5);
          $components[2] = $storage->mimetype;

          $headers = array_merge($headers, $storage->HTTPHeaders());

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
          $result['temp_uri'] = new StorageTempURI();
          $fp = fopen($result['temp_uri'], 'w');
          $curl_options[CURLOPT_FILE] = $fp;
        }

        break;

      case 'DELETE':
        $curl_options[CURLOPT_CUSTOMREQUEST] = 'DELETE';
        break;
    }

    if ($bucket) {
      $object_name = '/' . $object_name;
    }
    $components[] = '/' . $bucket . drupal_encode_path($object_name);

    $signature = $this->signature(implode("\n", $components));
    $headers[] = 'Authorization: AWS ' . $this->settings['access_key_id'] . ':' . $signature;

    $curl_options[CURLOPT_HTTPHEADER] = $headers;

    if ($bucket) {
      $bucket .= '.';
    }
    $ch = curl_init(url('https://' . $bucket . 's3.amazonaws.com' . drupal_encode_path($object_name), ['query' => $query]));
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

            watchdog('storage_s3', 'Amazon S3 error: @msg', ['@msg' => $msg], WATCHDOG_ERROR);

            if (user_access('administer storage')) {
              drupal_set_message(t('Amazon S3 error: @msg', ['@msg' => $msg]), 'error');
            }
          }
        }
      }
    }

    return $result;
  }

  /**
   * Gets RTMP data for the S3 object.
   *
   * @param string $s3_object
   *   The S3 object.
   *
   * @return array
   *   An array of RTMP data. Keys include:
   *   - streamer
   *   - file
   *
   * @throws \StorageException
   */
  private function rtmp($s3_object) {

    if ($this->settings['cloudfront_streaming'] && $this->settings['cloudfront_streaming_status'] == 'Deployed') {
      return [
        'streamer' => 'rtmp://' . $this->settings['cloudfront_streaming_domain'] . '/cfx/st',
        'file' => $s3_object,
      ];
    }

    throw new StorageException();
  }

  /**
   * Makes a CloudFront request.
   *
   * @param string $op
   *   The operation to perform. Valid values are: POST, PUT, GET, DELETE
   * @param bool $streaming
   *   (optional) TRUE to use streaming.
   * @param string|null $xml
   *   (optional) The XML payload for POST or PUT operations.
   *
   * @return array
   *   The request results.
   */
  private function requestCloudFront($op, $streaming = FALSE, $xml = NULL) {
    $result = [];
    $date = date(DATE_RFC1123, time());

    $headers[] = 'Date: ' . $date;

    $curl_options = [
      CURLOPT_HEADER => TRUE,
      CURLOPT_RETURNTRANSFER => TRUE,
      CURLOPT_AUTOREFERER => TRUE,
      CURLOPT_FOLLOWLOCATION => TRUE,
      CURLOPT_USERAGENT => 'Storage API (http://drupal.org/project/storage_api)',
    ];

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

          watchdog('storage_s3', 'Amazon CloudFront error: @msg', ['@msg' => $msg], WATCHDOG_ERROR);

          if (user_access('administer storage')) {
            drupal_set_message(t('Amazon CloudFront error: @msg', ['@msg' => $msg]), 'error');
          }
        }
      }
    }

    return $result;
  }

  /**
   * Gets the Amazon regions.
   *
   * @return array
   *   An associative array of Amazon regions.
   */
  private function locations() {

    return [
      '' => t('US Standard'),
      'us-west-1' => t('US West (Northern California)'),
      'us-west-2' => t('US West (Oregon)'),
      'eu-west-1' => t('EU (Ireland)'),
      // 'eu-central-1' => t('EU Central (Frankfurt)'),
      'ap-northeast-1' => t('Asia Pacific (Tokyo)'),
      'ap-southeast-1' => t('Asia Pacific (Singapore)'),
      'ap-southeast-2' => t('Asia Pacific (Sydney)'),
      'sa-east-1' => t('South America (Sao Paulo)'),
    ];
  }

  /**
   * Generates a caller reference ID.
   *
   * @return string
   *   The unique caller reference ID.
   */
  private function callerReference() {
    static $id;

    // Make sure we can create multiple distributions at the same time.
    return REQUEST_TIME . '-' . ++$id;
  }

  /**
   * Creates a CloudFront distribution.
   */
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

    $message = 'Added CloudFront distribution to storage container <i>' . check_plain($this->name()) . '</i>.';

    drupal_set_message($message);
    watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $this->path()));
  }

  /**
   * Creates a streaming CloudFront distribution.
   */
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

    $message = 'Added CloudFront streaming distribution to storage container <i>' . check_plain($this->name()) . '</i>.';

    drupal_set_message($message);
    watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $this->path()));
  }

  /**
   * Deploys a CloudFront distribution.
   */
  private function distributionDeploy() {
    $result = $this->requestCloudFront('GET');

    // If we can't get through to AWS then abort.
    if (!$result['http_code']) {
      return;
    }

    if ($result['http_code'] != 200) {
      $this->settings['cloudfront'] = FALSE;
      $message = 'Failed to deploy CloudFront distribution for storage container <i>' . check_plain($this->name()) . '</i>.';
    }
    else {
      $this->settings['cloudfront_status'] = (string) $result['simplexml']->Status;

      if ($this->settings['cloudfront_status'] != 'Deployed') {
        return;
      }

      $message = 'Deployed CloudFront distribution for storage container <i>' . check_plain($this->name()) . '</i>.';
    }

    $this->update();
    $this->flushServingContainer();

    watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $this->path()));
  }

  /**
   * Deploys a streaming CloudFront distribution.
   */
  private function distributionDeployStreaming() {
    $result = $this->requestCloudFront('GET', TRUE);

    // If we can't get through to AWS then abort.
    if (!$result['http_code']) {
      return;
    }

    if ($result['http_code'] != 200) {
      $this->settings['cloudfront_streaming'] = FALSE;
      $message = 'Failed to deploy CloudFront streaming distribution for storage container <i>' . check_plain($this->name()) . '</i>.';
    }
    else {
      $this->settings['cloudfront_streaming_status'] = (string) $result['simplexml']->Status;

      if ($this->settings['cloudfront_streaming_status'] != 'Deployed') {
        return;
      }
      $message = 'Deployed CloudFront streaming distribution for storage container <i>' . check_plain($this->name()) . '</i>.';
    }

    $this->update();
    $this->flushServingContainer();

    watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE, l(t('view'), $this->path()));
  }

  /**
   * Deletes a CloudFront distribution.
   *
   * @throws \StorageException
   */
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
        $message = 'Disabled CloudFront distribution for container <i>' . check_plain($this->name()) . '</i>.';
        watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE);
      }
    }
    elseif ($result['simplexml']->Status != 'InProgress') {
      $result = $this->requestCloudFront('DELETE');

      if ($result['http_code'] == 204) {
        $message = 'Deleted CloudFront distribution for container <i>' . check_plain($this->name()) . '</i>.';
        watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE);
        return;
      }
    }

    throw new StorageException();
  }

  /**
   * Deletes a streaming CloudFront distribution.
   *
   * @throws \StorageException
   */
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
        $message = 'Disabled CloudFront streaming distribution for container <i>' . check_plain($this->name()) . '</i>.';
        watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE);
      }
    }
    elseif ($result['simplexml']->Status != 'InProgress') {
      $result = $this->requestCloudFront('DELETE', TRUE);

      if ($result['http_code'] == 204) {
        $message = 'Deleted CloudFront streaming distribution for container <i>' . check_plain($this->name()) . '</i>.';
        watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE);
        return;
      }
    }

    throw new StorageException();
  }

  /**
   * {@inheritdoc}
   */
  public function serviceSettingsDefault() {
    return [
      'access_key_id' => '',
      'bucket' => '',
      'cname' => FALSE,
      'location' => '',
      'reduced_redundancy' => FALSE,
      'url_lifetime' => 60 * 60,
      'cloudfront' => FALSE,
      'cloudfront_streaming' => FALSE,
      'cache_control_max_age' => 60 * 5,
    ];
  }

  /**
   * {@inheritdoc}
   */
  public function serviceSettingsForm($already_exists) {
    $items['bucket_warning'] = [
      '#prefix' => '<div style="font-weight: bold;">',
      '#markup' => t('Do not use an existing bucket in S3. This will create the bucket for you with the settings needed for this module to function.'),
      '#suffix' => '</div>',
    ];
    $items['credentials_info'] = [
      '#prefix' => '<div style="font-weight: bold;">',
      '#markup' => t('Get your credentials') . ' ' . l(t('here'), 'http://aws-portal.amazon.com/gp/aws/developer/account/index.html?action=access-key', ['attributes' => ['target' => '_blank']]),
      '#suffix' => '</div>',
    ];

    $items['access_key_id'] = [
      '#type' => 'textfield',
      '#title' => t('Access key ID'),
      '#required' => TRUE,
      '#validated' => TRUE,
      '#default_value' => $this->settings['access_key_id'],
    ];

    $items['secret_access_key'] = [
      '#type' => 'password',
      '#title' => t('Secret access key'),
      '#required' => TRUE,
      '#validated' => TRUE,
    ];

    $items['bucket'] = [
      '#type' => 'textfield',
      '#title' => t('Bucket name'),
      '#required' => TRUE,
      '#validated' => TRUE,
      '#default_value' => $this->settings['bucket'],
      '#maxlength' => 63,
      '#description' => t('The bucket namespace is shared by all users of Amazon S3.') . '<br />' . t('If it contains any dots (e.g. it is a CNAME alias) it is not possible to serve as HTTPS as only a single level of subdomain matching is supported by a wildcard certificate.'),
    ];

    $items['cache_control_max_age'] = [
      '#type' => 'textfield',
      '#title' => 'Cache Control Header (max-age)',
      '#required' => TRUE,
      '#validated' => TRUE,
      '#default_value' => $this->settings['cache_control_max_age'],
      '#description' => t('Set max-age cache header, in seconds, to let caches know when an asset is expired.'),
      '#element_validate' => ['element_validate_integer_positive'],
    ];

    $replacements = [
      '!link' => l(t('Customizing Amazon S3 URLs with CNAMEs'), 'http://docs.amazonwebservices.com/AmazonS3/latest/dev/VirtualHosting.html', [
        'attributes' => ['target' => '_blank'],
      ]),
    ];
    $description = t('See !link. Currently ignored for CloudFront.', $replacements);

    $items['cname'] = [
      '#type' => 'checkbox',
      '#title' => t('Bucket name is CNAME alias'),
      '#description' => $description,
      '#default_value' => $this->settings['cname'],
    ];

    if (!$already_exists) {
      $items['location'] = [
        '#type' => 'select',
        '#title' => t('Region'),
        '#options' => $this->locations(),
        '#default_value' => $this->settings['location'],
      ];
    }

    $items['reduced_redundancy'] = [
      '#type' => 'checkbox',
      '#title' => t('Reduced redundancy'),
      '#default_value' => $this->settings['reduced_redundancy'],
    ];

    $items['cloudfront'] = [
      '#type' => 'checkbox',
      '#title' => t('Serve with CloudFront'),
      '#default_value' => $this->settings['cloudfront'],
    ];

    $items['cloudfront_streaming'] = [
      '#type' => 'checkbox',
      '#title' => t('Stream with CloudFront'),
      '#default_value' => $this->settings['cloudfront_streaming'],
    ];

    $items['cname_cf'] = [
      '#type' => 'textfield',
      '#title' => t('Cloudfront CNAME'),
      '#default_value' => (!empty($this->settings['cname_cf'])) ? $this->settings['cname_cf'] : '',
      '#maxlength' => 63,
      '#description' => t('Use CNAME for Cloudfront distribution.'),
    ];

    if ($this->access_control) {
      $description = t('(seconds)<br />This container will produce cryptographically signed URLs that will cease to function once their lifetime is over.');

      $items['url_lifetime'] = [
        '#type' => 'textfield',
        '#title' => t('URL lifetime'),
        '#required' => TRUE,
        '#validated' => TRUE,
        '#default_value' => $this->settings['url_lifetime'],
        '#description' => $description,
        '#size' => 8,
        '#maxlength' => 8,
      ];

      $items['cf_signed_notice_1'] = [
        '#markup' => t('This module will create the CloudFront Distribution but you must set the distribution to "Restricted Viewer Access" under the behaviors setting, once the distribution has been created. <br /><b>This is a crucial step that if not performed your content will not be protected!</b>'),
      ];

      $items['cf_signed_notice_2'] = [
        '#markup' => t('<p>If you have not created a Cloudfront Key Pair.  You must stop this process and go complete those steps first. Note: creating and setting up Cloudfront distribution changes can take a long time.') . ' ' . l(t('Link to AWS Security Credentials page'), 'https://console.aws.amazon.com/iam/home?#security_credential', ['external' => TRUE]) . '</p>',
      ];

      $items['cf_key_id'] = [
        '#type' => 'textfield',
        '#title' => t('Cloudfront key pair ID'),
        '#default_value' => (!empty($this->settings['cf_key_id'])) ? $this->settings['cf_key_id'] : '',
        '#description' => t('This is the Key Pair ID provided by Amazon for your Cloudfront key that you have created.'),
      ];

      $items['cf_key_contents'] = [
        '#type' => 'textarea',
        '#title' => t('Cloudfront Private key contents'),
        '#default_value' => (!empty($this->settings['cf_key_contents'])) ? $this->settings['cf_key_contents'] : '',
        '#description' => t('This is the contents of the Private Key downloaded from Amazon.  Paste in the ENTIRE contents of the downloaded PEM file. The file should start with "-----BEGIN RSA PRIVATE KEY-----" and end with "-----END RSA PRIVATE KEY-----".'),
      ];
    }

    return $items;
  }

  /**
   * {@inheritdoc}
   */
  public function serviceSettingsValidate($already_exists) {

    if (!$this->settings['access_key_id']) {
      form_set_error('settings][access_key_id', t('Access Key ID field is required.'));
    }

    if (!$this->settings['bucket']) {
      form_set_error('settings][bucket', t('Bucket name field is required.'));
    }
    // Halt processing if we we have empty form settings.
    if (form_get_errors()) {
      return;
    }

    // Test account credentials. HEAD request doesn't work.
    $result = $this->request('GET');
    // Test that the account can access the bucket.
    $this->result = $this->request('HEAD', $this->settings['bucket']);

    // Accounts may only have permissions to access buckets, we should ensure
    // that both requests return a 200 before assuming permission errors.
    if ($result['http_code'] != 200 && $this->result['http_code'] != 200) {
      form_set_error('settings][access_key_id', t('Access denied to S3 when testing account credentials.'));
      form_set_error('settings][secret_access_key', t('Access denied to S3 when testing account credentials.'));
      return;
    }

    // Check if bucket is already owned by this account.
    if (!$already_exists && $this->result['http_code'] == 200) {
      $result = $this->request('GET', $this->settings['bucket']);

      if (count($result['simplexml']->Contents) > 0) {
        form_set_error('settings][bucket', t('Bucket is already owned by this account, but is not empty.'));
      }
    }

    if ($already_exists && $this->result['http_code'] != 200) {
      form_set_error('settings][bucket', t('Bucket not owned by this account.'));
    }
  }

  /**
   * {@inheritdoc}
   */
  public function serviceCreate() {
    if ($this->result['http_code'] != 200) {
      $result = $this->request('PUT', $this->settings['bucket']);

      if ($result['http_code'] != 200) {
        throw new StorageException();
      }

      $message = 'Acquired S3 bucket <i>' . $this->settings['bucket'] . '</i>.';
      drupal_set_message($message);
      watchdog('storage_s3', $message, NULL, WATCHDOG_NOTICE);
    }
  }

  /**
   * {@inheritdoc}
   */
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

  /**
   * {@inheritdoc}
   */
  public function serviceUpdate() {
    if ($this->settings['cloudfront'] && !isset($this->settings['cloudfront_id'])) {
      $this->distributionCreate();
    }

    if ($this->settings['cloudfront_streaming'] && !isset($this->settings['cloudfront_streaming_id'])) {
      $this->distributionCreateStreaming();
    }
  }

  /**
   * {@inheritdoc}
   */
  public function serviceInfo() {
    $locations = $this->locations();

    $info = [
      t('Bucket') => $this->settings['bucket'],
      t('Bucket is CNAME alias') => $this->settings['cname'] ? t('Yes') : t('No'),
      t('Location') => isset($this->settings['location']) ? $locations[$this->settings['location']] : t('Unknown'),
      t('Reduced redundancy') => $this->settings['reduced_redundancy'] ? t('Yes') : t('No'),
    ];

    if ($this->access_control) {
      $info[t('URL lifetime')] = format_interval($this->settings['url_lifetime']);
    }
    else {
      if ($this->settings['cloudfront']) {

        if ($this->settings['cloudfront_status'] == 'Deployed') {
          $status = t('Yes');
        }
        else {
          $status = t('Setting up. This can take a few minutes.');
        }
      }
      else {
        $status = t('No');
      }

      $info[t('Serving with CloudFront')] = $status;

      if (isset($this->settings['cloudfront_status']) &&
        $this->settings['cloudfront_status'] == 'Deployed'
      ) {
        $info[t('CloudFront domain name')] = $this->settings['cloudfront_domain'];
        if (!empty($this->settings['cname_cf'])) {
          $info[t('CloudFront CNAME')] = $this->settings['cname_cf'];
        }
      }

      if ($this->settings['cloudfront_streaming']) {

        if ($this->settings['cloudfront_streaming_status'] == 'Deployed') {
          $status = t('Yes');
        }
        else {
          $status = t('Setting up. This can take a few minutes.');
        }
      }
      else {
        $status = t('No');
      }

      $info[t('Streaming with CloudFront')] = $status;

      if (isset($this->settings['cloudfront_streaming_status']) &&
        $this->settings['cloudfront_streaming_status'] == 'Deployed'
      ) {
        $info[t('CloudFront streaming domain name')] = $this->settings['cloudfront_streaming_domain'];
      }
    }

    return $info;
  }

  /**
   * {@inheritdoc}
   */
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

  /**
   * {@inheritdoc}
   */
  public function serviceMaintenance() {
    if ($this->settings['cloudfront'] && $this->settings['cloudfront_status'] != 'Deployed') {
      $this->distributionDeploy();
    }

    if ($this->settings['cloudfront_streaming'] && $this->settings['cloudfront_streaming_status'] != 'Deployed') {
      $this->distributionDeployStreaming();
    }
  }

  /**
   * {@inheritdoc}
   */
  public function serviceInstanceCreate(Storage $storage, $uri) {
    $reference = $this->uniqueFilename($storage->filename);

    $result = $this->request('PUT', $this->settings['bucket'], $reference, $storage, $uri);

    if ($result['http_code'] == 200) {
      return $reference;
    }
    else {
      throw new StorageException();
    }
  }

  /**
   * {@inheritdoc}
   */
  public function serviceInstanceCopy(Storage $storage, StorageContainerInterface $source_container) {
    $reference = $this->uniqueFilename($storage->filename);

    $storage->copy_source = '/' . $source_container->settings['bucket'] . '/' . $source_container->reference;
    $result = $this->request('PUT', $this->settings['bucket'], $reference, $storage);
    unset($storage->copy_source);

    if ($result['http_code'] == 200 && $result['simplexml']) {

      if ($result['simplexml']->getName() == 'CopyObjectResult') {
        return $reference;
      }
    }

    throw new StorageException();
  }

  /**
   * Converts an ETag to MD5 hash.
   *
   * @param string $etag
   *   The ETag.
   *
   * @return string
   *   The MD5 hash.
   */
  private function eTag2md5($etag) {
    return pack('H*', substr($etag, 1, 32));
  }

  /**
   * {@inheritdoc}
   */
  public function serviceManifest(array &$sandbox, &$finished) {

    $query = [
      'max-keys' => 100,
    ];

    if (isset($sandbox['marker'])) {
      $query['marker'] = $sandbox['marker'];
    }

    $result = $this->request('GET', $this->settings['bucket'], NULL, NULL, NULL, $query);

    foreach ($result['simplexml']->Contents as $object) {

      $manifest[(string) $object->Key] = [
        'size' => (string) $object->Size,
        'md5' => $this->eTag2md5($object->ETag),
      ];
    }

    $sandbox['marker'] = (string) $object->Key;
    $finished = $result['simplexml']->IsTruncated == 'false';

    return $manifest;
  }

  /**
   * {@inheritdoc}
   */
  public function serviceInstanceExists($reference) {
    $result = $this->request('HEAD', $this->settings['bucket'], $reference);
    return $result['http_code'] == 200;
  }

  /**
   * {@inheritdoc}
   */
  public function serviceInstanceCheck($reference) {
    $result = $this->request('HEAD', $this->settings['bucket'], $reference);

    if ($result['http_code'] != 200) {
      throw new StorageException();
    }

    return [
      'size' => $result['headers']['Content-Length'],
      'md5' => $this->eTag2md5($result['headers']['ETag']),
    ];
  }

  /**
   * {@inheritdoc}
   */
  public function serviceInstanceGetUri($reference) {

    $result = $this->request('GET', $this->settings['bucket'], $reference);

    // If the asset is not found, throw an appropriate exception.
    if ($result['http_code'] == 404) {
      throw new StorageException("Asset missing from S3");
    }
    return $result['temp_uri'];
  }

  /**
   * {@inheritdoc}
   */
  public function serviceInstanceDestroy($reference) {

    $result = $this->request('DELETE', $this->settings['bucket'], $reference);

    if ($result['http_code'] != 204) {
      throw new StorageException();
    }
  }

  /**
   * {@inheritdoc}
   */
  public function serviceInstanceServe($reference, $https) {
    $s3_object = '/' . drupal_encode_path($reference);
    $base_url = 'http' . ($https ? 's' : '') . '://';
    $query = [];

    if ($this->settings['cloudfront'] && $this->settings['cloudfront_status'] == 'Deployed') {
      $base_url .= ($this->settings['cname_cf']) ? $this->settings['cname_cf'] : $this->settings['cloudfront_domain'];
      if ($this->access_control) {
        $expires = time() + $this->settings['url_lifetime'];
        $policy = '{"Statement":[{"Resource":"' . $base_url . $s3_object . '","Condition":{"DateLessThan":{"AWS:EpochTime":' . $expires . '}}}]}';
        $query['Policy'] = str_replace(['+', '=', '/'], [
          '-',
          '_',
          '~',
        ], base64_encode($policy));
        $query['Signature'] = $this->signature($policy, TRUE);
        $query['Key-Pair-Id'] = $this->settings['cf_key_id'];
      }
    }
    else {
      if ($https && strpos($this->settings['bucket'], '.') !== FALSE) {
        $base_url .= 's3.amazonaws.com/' . $this->settings['bucket'];
      }
      else {
        $base_url .= $this->settings['bucket'];
        // If the bucket is a CNAME alias, the full domain name is not required
        // unless we are serving over https.
        if (!$this->settings['cname'] || $https) {
          $base_url .= '.s3.amazonaws.com';
        }
      }
      if ($this->access_control) {
        $query['AWSAccessKeyId'] = $this->settings['access_key_id'];
        $query['Expires'] = time() + $this->settings['url_lifetime'];

        $components = [
          'GET',
          '',
          '',
          $query['Expires'],
          '/' . $this->settings['bucket'] . $s3_object,
        ];

        $query['Signature'] = $this->signature(implode("\n", $components));
      }
    }

    return url($base_url . $s3_object, ['query' => $query]);
  }

}
