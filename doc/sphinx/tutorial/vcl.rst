Varnish Configuration Language - VCL
-------------------------------------

Varnish has a great configuration system. Most other systems use
configuration directives, where you basically turn on and off lots of
switches. Varnish uses a domain specific language called Varnish
Configuration Language, or VCL for short. Varnish translates this
configuration into binary code which is then executed when requests
arrive.

The VCL files are divided into subroutines. The different subroutines
are executed at different times. One is executed when we get the
request, another when files are fetched from the backend server.

Varnish will execute these subroutines of code at different stages of
its work. Because it is code it is execute line by line precedence
isn't a problem. At some point you call an action in this subroutine
and then the execution of the subroutine stops.

If you don't call an action in your subroutine and it reaches the end
Varnish will execute some built in VCL code. You will see this VCL
code commented out in default.vcl.

99% of all the changes you'll need to do will be done in two of these
subroutines. *vcl_recv* and *vcl_fetch*.

vcl_recv
~~~~~~~~

vcl_recv (yes, we're skimpy with characters, it's Unix) is called at
the beginning of a request, after the complete request has been
received and parsed.  Its purpose is to decide whether or not to serve
the request, how to do it, and, if applicable, which backend to use.

In vcl_recv you can also alter the request. Typically you can alter
the cookies and add and remove request headers.

Note that in vcl_recv only the request object, req is available.

vcl_fetch
~~~~~~~~~

vcl_fetch is called *after* a document has been successfully retrieved
from the backend. Normal tasks her are to alter the response headers,
trigger ESI processing, try alternate backend servers in case the
request failed.

In vcl_fetch you still have the request object, req, available. There
is also a *backend response*, beresp. beresp will contain the HTTP
headers from the backend.


actions
~~~~~~~

The most common actions to return are these:

*pass*
 When you return pass the request and subsequent response will be passed to
 and from the backend server. It won't be cached. pass can be returned from
 vcl_recv

*hit_for_pass*
  Similar to pass, but accessible from vcl_fetch. Unlike pass, hit_for_pass
  will create a hitforpass object in the cache. This has the side-effect of
  caching the decision not to cache. This is to allow would-be uncachable
  requests to be passed to the backend at the same time. The same logic is
  not necessary in vcl_recv because this happens before any potential
  queueing for an object takes place.

*lookup*
  When you return lookup from vcl_recv you tell Varnish to deliver content 
  from cache even if the request othervise indicates that the request 
  should be passed. You can't return lookup from vcl_fetch.

*pipe*
  Pipe can be returned from vcl_recv as well. Pipe short circuits the
  client and the backend connections and Varnish will just sit there
  and shuffle bytes back and forth. Varnish will not look at the data being 
  send back and forth - so your logs will be incomplete. 
  Beware that with HTTP 1.1 a client can send several requests on the same 
  connection and so you should instruct Varnish to add a "Connection: close"
  header before actually returning pipe. 

*deliver*
 Deliver the cached object to the client.  Usually returned from vcl_fetch. 

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

Operators
~~~~~~~~~

The following operators are available in VCL. See the examples further
down for, uhm, examples.

= 
 Assignment operator.

== 
 Comparison.

~
 Match. Can either be used with regular expressions or ACLs.

!
 Negation.

&&
 Logical *and*

||
 Logical *or*

Example 1 - manipulating headers
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Lets say we want to remove the cookie for all objects in the /static
directory of our web server:::

  sub vcl_recv {
    if (req.url ~ "^/images") {
      unset req.http.cookie;
    }
  }

Now, when the request is handled to the backend server there will be
no cookie header. The interesting line is the one with the
if-statement. It matches the URL, taken from the request object, and
matches it against the regular expression. Note the match operator. If
it matches the Cookie: header of the request is unset (deleted). 

Example 2 - manipulating beresp
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Here we override the TTL of a object comming from the backend if it
matches certain criteria:::

  sub vcl_fetch {
     if (req.url ~ "\.(png|gif|jpg)$") {
       unset beresp.http.set-cookie;
       set beresp.ttl = 1h;
    }
  }

Example 3 - ACLs
~~~~~~~~~~~~~~~~

You create a named access control list with the *acl* keyword. You can match
the IP address of the client against an ACL with the match operator.::

  # Who is allowed to purge....
  acl local {
      "localhost";
      "192.168.1.0"/24; /* and everyone on the local network */
      ! "192.168.1.23"; /* except for the dialin router */
  }
  
  sub vcl_recv {
    if (req.request == "PURGE") {
      if (client.ip ~ local) {
         return(lookup);
      }
    } 
  }
  
  sub vcl_hit {
     if (req.request == "PURGE") {
       set obj.ttl = 0s;
       error 200 "Purged.";
      }
  }

  sub vcl_miss {
    if (req.request == "PURGE") {
      error 404 "Not in cache.";
    }
  }

