
Requests, responses and objects
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In VCL, there several important objects.


*req*
 The request object. When Varnish has received the request the req object is 
 created and populated. Most of the work you do in vcl_recv you 
 do on or with the req object.

*bereq*
 The backend request object. 

*beresp*
 The backend respons object. It contains the headers of the object 
 coming from the backend. Most of the work you do in vcl_fetch you 
 do on the beresp object.

*resp*
 The HTTP response right before it is delivered to the client. 

*obj* 
 The object as it is stored in cache. Mostly read only.
