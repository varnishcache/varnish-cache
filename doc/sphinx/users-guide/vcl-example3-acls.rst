
ACLs
~~~~

You create a named access control list with the *acl* keyword. You can match
the IP address of the client against an ACL with the match operator.::

  # Who is allowed to purge....
  acl local {
      "localhost";
      "192.168.1.0"/24; /* and everyone on the local network */
      ! "192.168.1.23"; /* except for the dialin router */
  }
  
  sub vcl_recv {
    if (req.method == "PURGE") {
      if (client.ip ~ local) {
         return(lookup);
      }
    } 
  }
  
  sub vcl_hit {
     if (req.method == "PURGE") {
       set obj.ttl = 0s;
       error 200 "Purged.";
      }
  }

  sub vcl_miss {
    if (req.method == "PURGE") {
      error 404 "Not in cache.";
    }
  }
