Varnish Configuration Language - VCL
-------------------------------------

Varnish has a really neat configuration system. Most other systems use
configuration directives, where you basically turn on and off a bunch
of switches. 

A very common thing to do in Varnish is to override the cache headers
from our backend. Lets see how this looks in Squid, which has a
standard configuration.::

	 refresh_pattern ^http://images.   3600   20%     7200
	 refresh_pattern -i (/cgi-bin/|\?)    0    0%        0
	 refresh_pattern -i (/\.jpg)       1800   10%     3600 override-expire 
	 refresh_pattern .                    0   20%     4320

If you are familiar with squid that probably made sense to you. But
lets point out a few weaknesses with this model.

1) It's not intuitive. You can guess what the options mean, and you
   can (and should) document it in your configuration file.

2) Which rules have precedence? Does the last rule to match stick? Or
   the first? Or does Squid try to combine all the matching rules. I
   actually don't know. 

Now enter Varnish. Varnish takes your configuration file and
translates it to C code, then runs it through a compiler and loads
it. When requests come along varnish just executes the relevant
subroutines of the configuration at the relevant times.

Varnish will execute these subroutines of code at different stages of
its work. Since its code it's execute line by line and precedence
isn't a problem. At some point you call an action in this subroutine
and then the execution of the subroutine stops. 

If you don't call an action in your subroutine and it reaches the end
Varnish will execute some built in code as well. We discuss this in
XXX: Appendix A - the builtin VCL.

99% of all the changes you'll need to do will be done in two of these
subroutines.

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
