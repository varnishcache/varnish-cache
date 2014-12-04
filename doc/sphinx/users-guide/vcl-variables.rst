
Request and response VCL objects
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. XXX: refactored headline. benc

In VCL, there several important objects that you need to be aware of. These
objects can be accessed and manipulated using VCL.


*req*
 The request object. When Varnish has received the request the `req` object is
 created and populated. Most of the work you do in `vcl_recv` you
 do on or with the `req` object.

*bereq*
 The backend request object. Varnish contructs this before sending it to the
 backend. It is based on the `req` object.

.. XXX:in what way? benc

*beresp*
 The backend response object. It contains the headers of the object
 coming from the backend. If you want to modify the response coming from the
 server you modify this object in `vcl_backend_response`.

*resp*
 The HTTP response right before it is delivered to the client. It is
 typically modified in `vcl_deliver`.

*obj* 
 The object as it is stored in cache. Read only.

.. XXX:What object? the current request? benc

