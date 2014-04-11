
.. _glossary:

Varnish Glossary
================

.. glossary:: 
   :sorted:

   ..
	This file will be sorted automagically during formatting,
	so we keep the source in subject order to make sure we
	cover all bases.

   .. comment: "components of varnish --------------------------------"

   varnishd (NB: with 'd')
	This is the actual Varnish cache program.  There is only
	one program, but when you run it, you will get *two*
	processes:  The "master" and the "worker" (or "child").

   master (process)
	One of the two processes in the varnishd program.
	The master proces is a manager/nanny process which handles
	configuration, parameters, compilation of :term:VCL etc.
	but it does never get near the actual HTTP traffic.

   worker (process)
	The worker process is started and configured by the master
	process.  This is the process that does all the work you actually
	want varnish to do.  If the worker dies, the master will try start
	it again, to keep your website alive.

   backend
	The HTTP server varnishd is caching for.  This can be
	any sort of device that handles HTTP requests, including, but
	not limited to: a webserver, a CMS, a load-balancer
	another varnishd, etc.

   client
	The program which sends varnishd an HTTP request, typically
	a browser, but do not forget to think about spiders, robots
	script-kiddies and criminals.

   varnishstat
	Program which presents varnish statistics counters.

   varnishlog
	Program which presents varnish transaction log in native format.

   varnishtop
	Program which gives real-time "top-X" list view of transaction log.

   varnishncsa
	Program which presents varnish transaction log in "NCSA" format.

   varnishhist
	Eye-candy program showing responsetime histogram in 1980ies
	ASCII-art style.

   varnishtest
	Program to test varnishd's behaviour with, simulates backend
	and client according to test-scripts.

   .. comment: "components of traffic ---------------------------------"

   header
	An HTTP protocol header, like "Accept-Encoding:".

   request
	What the client sends to varnishd and varnishd sends to the backend.

   response
	What the backend returns to varnishd and varnishd returns to
	the client.  When the response is stored in varnishd's cache,
	we call it an object.

   backend response
        The response specifically served from a backend to
        varnishd. The backend response may be manipulated in
        vcl_backend_response.

   body
	The bytes that make up the contents of the object, varnishd
	does not care if they are in HTML, XML, JPEG or even EBCDIC,
	to varnishd they are just bytes.

   object
	The (possibly) cached version of a backend response. Varnishd
	receives a reponse from the backend and creates an object,
	from which it may deliver cached responses to clients. If the
	object is created as a result of a request which is passed, it
	will not be stored for caching.

   .. comment: "configuration of varnishd -----------------------------"

   VCL
	Varnish Configuration Language, a small specialized language
	for instructing Varnish how to behave.

   .. comment: "actions in VCL ----------------------------------------"

   hit
	An object Varnish delivers from cache.

   miss
	An object Varnish fetches from the backend before it is served
	to the client.  The object may or may not be put in the cache,
	that depends.

   pass
	An object Varnish does not try to cache, but simply fetches
	from the backend and hands to the client.

   pipe
	Varnish just moves the bytes between client and backend, it
	does not try to understand what they mean.

