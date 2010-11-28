.. _tutorial-esi:

Edge Side Includes
------------------

*Edge Side Includes* is a language to include *fragments* of web pages
in other web pages. Think of it as HTML include statement that works
over HTTP. 

On most web sites a lot of content is shared between
pages. Regenerating this content for every page view is wasteful and
ESI tries to address that letting you decide the cache policy for
each fragment individually.

In Varnish we've only implemented a small subset of ESI. As of 2.1 we
have three ESI statements:

 * esi:include 
 * esi:remove
 * <!--esi ...-->

Content substitution based on variables and cookies is not implemented
but is on the roadmap. 

Example: esi include
~~~~~~~~~~~~~~~~~~~~

Lets see an example how this could be used. This simple cgi script
outputs the date:::

     #!/bin/sh
     
     echo 'Content-type: text/html'
     echo ''
     date "+%Y-%m-%d %H:%M"

Now, lets have an HTML file that has an ESI include statement:::

     <HTML>
     <BODY>
     The time is: <esi:include src="/cgi-bin/date.cgi"/>
     at this very moment.
     </BODY>
     </HTML>

For ESI to work you need to activate ESI processing in VCL, like this:::

    sub vcl_fetch {
    	if (req.url == "/test.html") {
           esi;        		     /* Do ESI processing		*/
           set obj.ttl = 24 h; 	     /* Sets the TTL on the HTML above  */
    	} elseif (req.url == "/cgi-bin/date.cgi") {
           set obj.ttl = 1m;         /* Sets a one minute TTL on	*/
	       	       	 	     /*  the included object		*/
        }
    }

Example: esi remove
~~~~~~~~~~~~~~~~~~~

The *remove* keyword allows you to remove output. You can use this to make
a fall back of sorts, when ESI is not available, like this:::

  <esi:include src="http://www.example.com/ad.html"/> 
  <esi:remove> 
    <a href="http://www.example.com">www.example.com</a>
  </esi:remove>

Example: <!--esi ... -->
~~~~~~~~~~~~~~~~~~~~~~~~


This is a special construct to allow HTML marked up with ESI to render
without processing. ESI Processors will remove the start ("<!--esi")
and end ("-->") when the page is processed, while still processing the
contents. If the page is not processed, it will remain, becoming an
HTML/XML comment tag. For example::

  <!--esi  
  <p>Warning: ESI Disabled!</p>
  </p>  -->

This assures that the ESI markup will not interfere with the rendering
of the final HTML if not processed.


