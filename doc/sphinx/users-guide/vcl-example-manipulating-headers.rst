


Manipulating request headers in VCL
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Lets say we want to remove the cookie for all objects in the `/images`
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

