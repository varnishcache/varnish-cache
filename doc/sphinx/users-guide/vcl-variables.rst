
Requests, responses and objects
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In VCL, there are three important data structures. The request, coming
from the client, the response coming from the backend server and the
object, stored in cache.

In VCL you should know the following structures.

*req*
 The request object. When Varnish has received the request the req object is 
 created and populated. Most of the work you do in vcl_recv you 
 do on or with the req object.

*beresp*
 The backend respons object. It contains the headers of the object 
 comming from the backend. Most of the work you do in vcl_fetch you 
 do on the beresp object.

*obj*
 The cached object. Mostly a read only object that resides in memory. 
 obj.ttl is writable, the rest is read only.
