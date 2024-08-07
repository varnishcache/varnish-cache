..
	Copyright (c) 2012-2020 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

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

Content substitution based on variables and cookies is not implemented.

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

Note that ``set beresp.do_esi = true;`` is not required, and should
be avoided, for the included fragments, unless they also contains
``<ESI::include …/>`` instructions.

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

What happens when it fails ?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

By default, the fragments must have ``resp.status`` 200 or 204 or
their delivery will be considered failed.

Likewise, if the fragment is a streaming fetch, and that fetch
fails, the fragment delivery is considered failed.

If you include synthetic fragments, that is fragments created in
``vcl_backend_error{}`` or ``vcl_synth{}``, you must set
``(be)resp.status`` to 200 before ``return(deliver);``, for example
with a ``return (synth(200))`` or ``return (error(200))`` transition.

Failure to properly deliver an ESI fragment has no effect on its
parent request delivery by default. The parent request can include
the ESI fragment with an ``onerror`` attribute::

    <ESI:include src="…" onerror="continue"/>

This attribute is ignored by default. In fact, Varnish will treat
failures to deliver ESI fragments as if there was the attribute
``onerror="continue"``. In the absence of this attribute with this
specific value, Varnish should normally abort the delivery of the
parent request.

We say "abort" rather than "fail", because by the time Varnish
starts inserting the fragments, the HTTP response header has long
since been sent, and it is no longer possible to change the parent
requests's ``resp.status`` to a 5xx, so the only way to signal that
something is amiss, is to close the connection in the HTTP/1 case or
reset the stream for h2 sessions.

However, it is possible to allow individual ``<ESI:include…`` to
continue in case of failures, by setting::

    param.set feature +esi_include_onerror

Once this feature flag is enabled, a delivery failure can only continue
if an ``onerror`` attribute said so. The ESI specification states that
in that case the failing fragment is not delivered, which is honored based
on the status code, or based on the response body only when streaming is
disabled (see ``beresp.do_stream``).

Can an ESI fragment also use ESI-includes ?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes, but the depth is limited by the ``max_esi_depth``
parameter in order to prevent infinite recursion.

Doing ESI on JSON and other non-XML'ish content
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Varnish will peek at the first byte of an object and if it is not
a "<" Varnish assumes you didn't really mean to ESI process it.
You can disable this check by::

   param.set feature +esi_disable_xml_check

Ignoring BOM in ESI objects
~~~~~~~~~~~~~~~~~~~~~~~~~~~

If you backend spits out a Unicode Byte-Order-Mark as the first
bytes of the response, the "<" check will fail unless you set::

   param.set feature +esi_remove_bom

ESI on invalid XML
~~~~~~~~~~~~~~~~~~

The ESI parser expects the XML to be reasonably well formed, but
this may fail if you are ESI including non-XML files.  You can
make the ESI parser disregard anything but ESI tags by setting::

   param.set feature +esi_ignore_other_elements

ESI includes with HTTPS protocol
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If ESI:include tags specify HTTPS protocol, it will be ignored
by default, because Varnish has no way to fetch it with encryption.
If you want Varnish to fetch them like it does anything else, set::

   param.set feature +esi_ignore_https

ESI on partial responses (206)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Varnish supports range requests, but in general partial responses
make no sense in an ESI context.

If you really know what you are doing, change the 206 to a 200::

   sub vcl_backend_response {
       if (beresp.status == 206 && beresp.http.secret == "swordfish") {
           set beresp.do_esi = True;
           set beresp.status = 200;
       }
   }

ESI and return(vcl(...))
~~~~~~~~~~~~~~~~~~~~~~~~

If the original client request switched to a different VCL using
``return(vcl(...))`` in ``vcl_recv``, any esi:include-requests
will still start out in the same VCL as the original did, *not*
in the one it switched to.

ESI and gzip compression
~~~~~~~~~~~~~~~~~~~~~~~~

Varnish's ESI implementation handles gzip compression automatically,
no matter how it is mixed:  The parent request can be compressed
or uncompressed and the fragments can be compressed or uncompressed,
it all works out.

Varnish does this compressing all parts of ESI responses
separately, and stitching them together on the fly during
delivery, which has a negative impact on compression ratio.

When you ``set beresp.do_esi = True;`` on a gzip'ed response, it
will be uncompressed and recompressed part-wise during the fetch.

The part-wise compression reduces the opportunities for
removing redundancy, because back-references in the gzip
data stream cannot point outside its own part.

The other case where compression ratio is impacted, is if an
uncompressed fragment is inserted into a compressed
response.
