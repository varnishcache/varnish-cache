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
