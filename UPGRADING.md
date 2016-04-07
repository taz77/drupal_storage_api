Storage API Upgrade Guide
===============================

1.8 to 1.9
----------

The Storage API has undergone many changes with the Google Service provider. 
These changes were driven by the updates to the Google PHP API library.

First change is the requirement to have a private file system. The Google PHP
API requires a credential file to read from. This file, for security, must reside
in a private file system. Default installation of Drupal does not establish a 
private file system.

**Before**

