Storaga API Module

The storage API module creates an agnostic form of storingin files withing
Drupal.  You can create containers and classes that will give you flexibility
in storying information from Drupal and even creating redundant methods of
storing information.


== 3rd Party Storage Providers ==

Rackspace Cloudfiles Provider:
This service requires an accout with the external service provider.

Amazon S3 Provider:
This service requires an accout with the external service provider. Please refer
to the online documentation for additional help:
https://www.drupal.org/node/656716

Amazon S3 is a redundant storage service.
Directly serving web content from an S3 should NEVER be done for a website
focused on any level of performance.  S3 is NOT designed for performance!!!
Amazon clearly declares this in their documentation.

S3 is for storage! It is designed for data integretity and redundancy.  If you
desire to have a high performance website with assests stored in S3 you must
use Cloudfront to deploy the assets stored in S3.  Cloudfront setup isn't for
the faint of heart, it is a CDN that requires extensive knowlege of the HTTP
protocol to optimize.

If you don't understand TCP and HTTP headers do not expect Cloudfront + S3 to
work well for you. Due to the complexity we will not offer support for this in
the issue queue - any request will be marked as "Will Not Fix".  Suggest
looking at other commercial options for a CDN such as Akamai or Edgecast where
they will provide custom support to make your application run fas.
