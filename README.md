Storaga API Module
================================================================================

The storage API module creates an agnostic form of storingin files withing
Drupal.  You can create containers and classes that will give you flexibility
in storying information from Drupal and even creating redundant methods of
storing information.


3rd Party Storage Providers
================================================================================

Rackspace Cloudfiles Provider
--------------------------------------------------------------------------------
This service requires an accoutt with the external service provider.
**Warning**: this storage provider implementation is old. If you are a developer
we would gladly accept updates to this provider.

Amazon S3 Provider
--------------------------------------------------------------------------------
This service requires an accout with the external service provider. Please refer
to the online documentation for additional help:
[https://www.drupal.org/node/656716](https://www.drupal.org/node/656716)

Amazon S3 is a redundant storage service. Directly serving web content from an 
S3 should NEVER be done for a website focused on any level of performance.  S3 
is NOT designed for performance! Amazon clearly declares this in their 
documentation.

S3 is for storage! It is designed for data integrity and redundancy.  If you
desire to have a high performance website with assests stored in S3 you must
use Cloudfront to deploy the assets stored in S3.  Cloudfront setup isn't for
the faint of heart, it is a CDN that requires extensive knowlege of the HTTP
protocol to optimize.

If you don't understand TCP and HTTP headers do not expect Cloudfront + S3 to
work well for you. Due to the complexity we will not offer support for this in
the issue queue - any request will be marked as "Will Not Fix".  Suggest
looking at other commercial options for a CDN such as Akamai or Edgecast where
they will provide custom support to make your application run fast.

Google Cloud Storage Service
--------------------------------------------------------------------------------
This service requires an account with the external service provider. Visit
Google's [product](https://cloud.google.com/storage/) page for more details.

To use this service you must have a private filesystem established in your
Drupal installation. The private file system is used to store your service
credentials and for security should this should not reside in the public file
system.

Additionally you must have the Google API PHP Client library installed. The
easiest way to do this is to use Composer and [Composer Manager](https://www.drupal.org/project/composer_manager) module for Drupal.

If you are using Composer Manager. Simply use Drush to enable the module and
Composer Manager will load everything you need.

###Manual Installation###
####Downloading Files####
In order to install this library without the use of Libraries module or Composer
follow Google's [instructions](https://github.com/google/google-api-php-client#download-the-release) for downloading the package.
You will then have to include the file by editing services/google.inc and uncomment
the line for the require once and alter the path to the downloaded zip package.
The package will have to be somewhere in your Drupal installation. The code to
edit is contained in the `private function init()` function. 
####Libraries Module####
Download the latest release of [the PHP api](https://github.com/google/google-api-php-client) and place the file in
your libraries folder under the folder named `google-api-php-client`. An example
path would be /sites/all/libraries/google-api-php-client.

