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
subroutines. vcl_recv and vcl_fetch.

vcl_recv
~~~~~~~~

vcl_recv (yes, we're skimpy with characters, it's Unix) is called at
the beginning of a request, after the complete request has been
received and parsed.  Its purpose is to decide whether or not to serve
the request, how to do it, and, if applicable, which backend to use.

In vcl_recv you can also alter the request, dropping cookies, rewrite
headers.

vcl_fetch
~~~~~~~~~

vcl_fetch is called *after* a document has been successfully retrieved
from the backend. Normal tasks her are to alter the response headers,
trigger ESI processing, try alternate backend servers in case the
request failed.

actions
~~~~~~~

The most common actions to call are these:

*pass*
 When you call pass the request and subsequent response will be passed
 to and from the backend server. It won't be cached. pass can be called 
 in both vcl_recv and vcl_fetch.

*lookup*
  When you call lookup from vcl_recv you tell Varnish to deliver content 
  from cache even if the request othervise indicates that the request 
  should be passed. You can't call lookup from vcl_fetch.

*pipe*
 

*deliver*

*esi*
 ESI-process the fetched document.

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
if-statement. It probably needs a bit of explaining. Varnish has a few
objects that are available throughout the VCL. The important ones are:

*req*
 The request object. Each HTTP transaction contains a request and a 
 response. When Varnish has recieved the request the req object is 
 created and populated. Most of the work you do in vcl_fetch you 
 do on or with the req object.

*beresp*
 The backend respons object. It contains the headers of the object 
 comming from the backend. Most of the work you do in vcl_fetch you 
 do on the beresp object.

Example 2 - manipulating beresp
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Here we override the TTL of a object comming from the backend if it
matches certain criteria:::

  sub vcl_fetch {
     if (beresp.url ~ "\.(png|gif|jpg)$") {
       unset beresp.http.set-cookie;
       beresp.ttl = 3600;
    }
  }

Example 3 - ACLs
~~~~~~~~~~~~~~~~
