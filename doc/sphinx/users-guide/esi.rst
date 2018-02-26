.. _users-guide-esi:

Content composition with Edge Side Includes
-------------------------------------------

Varnish can create web pages by assembling different pages, called `fragments`,
together into one page. These `fragments` can have individual cache policies.
If you have a web site with a list showing the five most popular articles on
your site, this list can probably be cached as a `fragment` and included
in all the other pages.

.. XXX:What other pages? benc

Used properly this strategy can dramatically increase
your hit rate and reduce the load on your servers.

In Varnish we've only implemented a small subset of ESI, because most of
the rest of the ESI specifications facilities are easier and better done
with VCL::

 esi:include
 esi:remove
 <!--esi ...-->

Content substitution based on variables and cookies is not implemented
but is on the roadmap. At least if you look at the roadmap from a
certain angle. During a full moon.

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

    sub vcl_backend_response {
    	if (bereq.url == "/test.html") {
           set beresp.do_esi = true; // Do ESI processing
           set beresp.ttl = 24 h;    // Sets the TTL on the HTML above
    	} elseif (bereq.url == "/cgi-bin/date.cgi") {
           set beresp.ttl = 1m;      // Sets a one minute TTL on
	       	       	 	     // the included object
        }
    }

Example: esi:remove and <!--esi ... -->
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The `<esi:remove>` and `<!--esi ... -->` constructs can be used to present
appropriate content whether or not ESI is available, for example you can
include content when ESI is available or link to it when it is not.
ESI processors will remove the start ("<!--esi") and the end ("-->") when
the page is processed, while still processing the contents. If the page
is not processed, it will remain intact, becoming a HTML/XML comment tag.
ESI processors will remove `<esi:remove>` tags and all content contained
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

Footnotes about ESI
-------------------

Doing ESI on JSON and other non-XML'ish content
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Varnish will peek at the first byte of an object and if it is not
a "<" Varnish assumes you didn't really mean to ESI process.
You can alter this behaviour by::

   param.set feature +esi_disable_xml_check

Ignoring BOM in ESI objects
~~~~~~~~~~~~~~~~~~~~~~~~~~~

If you backend spits out a Unicode Byte-Order-Mark as the first
bytes of the reponse, the "<" check will fail unless you set::

   param.set feature +esi_remove_bom

ESI on invalid XML
~~~~~~~~~~~~~~~~~~

The ESI parser expects the XML to be reasonably well formed, but
this may fail if you are ESI including non-XML files.  You can
make the ESI parser disrecard anything but ESI tags by setting:

   param.set feature +esi_ignore_other_elements

ESI includes with HTTPS protocol
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If ESI:include tags specify HTTPS protocol, it will be ignored
by default, because varnish has no way to fetch it encryption
enabled.  If you want to treat HTTPS in ESI:include tags as if
it were HTTP, set::

   param.set feature +esi_ignore_https

ESI on partial responses (206)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Varnish can ``pass`` range requests but it is ESI processing a partial
response makes no sense, so the fetch fails if you do ask for that.
If you really know what you are doing, you can use this workaround::

   sub vcl_backend_response {
       if (beresp.status == 206 && beresp.http.secret == "swordfish") {
           set beresp.do_esi = True;
           set beresp.status = 1206;
       }
   }
