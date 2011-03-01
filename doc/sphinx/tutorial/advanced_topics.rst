.. _tutorial-advanced_topics:

Advanced topics
---------------

This tutorial has covered the basics in Varnish. If you read through
it all you should now have the skills to run Varnish.

Here is a short overview of topics that we haven't covered in the tutorial. 

More VCL
~~~~~~~~

VCL is a bit more complex then what we've covered so far. There are a
few more subroutines available and there a few actions that we haven't
discussed. For a complete(ish) guide to VCL have a look at the VCL man
page - ref:`reference-vcl`.

Using In-line C to extend Varnish
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You can use *in-line C* to extend Varnish. Please note that you can
seriously mess up Varnish this way. The C code runs within the Varnish
Cache process so if your code generates a segfault the cache will crash.

One of the first uses I saw of In-line C was logging to syslog.::

	# The include statements must be outside the subroutines.
	C{
		#include <syslog.h>
        }C
	
        sub vcl_something {
                C{
		        syslog(LOG_INFO, "Something happened at VCL line XX.");
	        }C
        }


Edge Side Includes
~~~~~~~~~~~~~~~~~~

Varnish can cache create web pages by putting different pages
together. These *fragments* can have individual cache policies. If you
have a web site with a list showing the 5 most popular articles on
your site, this list can probably be cached as a fragment and included
in all the other pages. Used properly it can dramatically increase
your hit rate and reduce the load on your servers. ESI looks like this::

  <HTML>
  <BODY>
  The time is: <esi:include src="/cgi-bin/date.cgi"/>
  at this very moment.
  </BODY>
  </HTML>

ESI is processed in vcl_fetch by using the *esi* keyword.::

  sub vcl_fetch {
      if (req.url == "/test.html") {
	  esi;  /* Do ESI processing */
      }
  }
