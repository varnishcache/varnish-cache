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

Varnish will not process ESI instructions in HTML comments.

Example: esi:include
~~~~~~~~~~~~~~~~~~~~

Lets see an example how this could be used. This simple cgi script
outputs the date::

     #!/bin/sh
     
     echo 'Content-type: text/html'
     echo ''
     date "+%Y-%m-%d %H:%M"

Now, lets have an HTML file that has an ESI include statement::

     <HTML>
     <BODY>
     The time is: <esi:include src="/cgi-bin/date.cgi"/>
     at this very moment.
     </BODY>
     </HTML>

For ESI to work you need to activate ESI processing in VCL, like this::

    sub vcl_fetch {
    	if (req.url == "/test.html") {
           set beresp.do_esi = true; /* Do ESI processing		*/
           set beresp.ttl = 24 h;    /* Sets the TTL on the HTML above  */
    	} elseif (req.url == "/cgi-bin/date.cgi") {
           set beresp.ttl = 1m;      /* Sets a one minute TTL on	*/
	       	       	 	     /*  the included object		*/
        }
    }

Example: esi:remove and <!--esi ... -->
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The <esi:remove> and <!--esi ... --> constructs can be used to present
appropriate content whether or not ESI is available, for example you can
include content when ESI is available or link to it when it is not.
ESI processors will remove the start ("<!--esi") and end ("-->") when
the page is processed, while still processing the contents. If the page
is not processed, it will remain, becoming an HTML/XML comment tag.
ESI processors will remove <esi:remove> tags and all content contained
in them, allowing you to only render the content when the page is not
being ESI-processed.
For example::

  <esi:remove> 
    <a href="http://www.example.com/LICENSE">The license</a>
  </esi:remove>
  <!--esi  
  <p>The full text of the license:</p>
  <esi:include src="http://example.com/LICENSE" />
  -->
