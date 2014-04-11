.. _user-guide-vcl_actions:

actions
~~~~~~~

The most common actions to return are these:

.. XXX:Maybe a bit more explanation here what is an action and how it is returned? benc

*pass*
  When you return pass the request and subsequent response will be passed to
  and from the backend server. It won't be cached. `pass` can be returned from
  `vcl_recv`.

*hash*
  When you return hash from `vcl_recv` you tell Varnish to deliver content 
  from cache even if the request othervise indicates that the request 
  should be passed. 

*pipe*
.. XXX:What is pipe? benc

  Pipe can be returned from `vcl_recv` as well. Pipe short circuits the
  client and the backend connections and Varnish will just sit there
  and shuffle bytes back and forth. Varnish will not look at the data being 
  send back and forth - so your logs will be incomplete. 

*deliver*
  Deliver the object to the client. Usually returned from `vcl_backend_response`. 

*restart*
  Restart processing of the request. You can restart the processing of
  the whole transaction. Changes to the `req` object are retained.

*retry*
  Retry the request against the backend. This can be returned from
  `vcl_backend_response` or `vcl_backend_error` if you don't like the response 
  that the backend delivered.
