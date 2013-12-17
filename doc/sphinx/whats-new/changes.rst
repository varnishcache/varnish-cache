.. _whatsnew_changes:

Changes in Varnish 4
====================

Varnish 4 is quite an extensive update over Varnish 3, with some very big improvements to central parts of varnish.

Client/backend split
--------------------
In the past, Varnish has fetched the content from the backend in the same
thread as the client request. The client and backend code has now been split,
allowing for some much requested improvements.
This split allows varnish to refresh content in the background while serving
stale content quickly to the client.

This split has also necessitated a change of the VCL-functions, in particular functionality has moved from the old vcl_fetch method to the two new methods vcl_backend_fetch and vcl_backend_response.
