Example 1 - manipulating headers
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Lets say we want to remove the cookie for all objects in the /images
directory of our web server::

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
matches certain criteria::

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

