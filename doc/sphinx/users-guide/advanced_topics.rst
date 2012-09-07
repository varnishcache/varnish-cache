.. _users-guide-advanced_topics:

Advanced topics
---------------

This guide has covered the basics in Varnish. If you read through
it all you should now have the skills to run Varnish.

Here is a short overview of topics that we haven't covered in the guide. 

More VCL
~~~~~~~~

VCL is a bit more complex then what we've covered so far. There are a
few more subroutines available and there a few actions that we haven't
discussed. For a complete(ish) guide to VCL have a look at the VCL man
page - ref:`reference-vcl`.

Using In-line C to extend Varnish
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

(Here there be dragons)

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


