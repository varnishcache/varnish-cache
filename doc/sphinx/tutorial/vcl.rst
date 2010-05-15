Varnish Configuration Language - VCL
==============================

How ordinary configuration files work
---------------------------------

Varnish has a really neat configuration system. Most other systems use
configuration directives, where you basically turn on and off a bunch
of switches. 

A very common thing to do in Varnish is to override the cache headers
from our backend. Lets see how this looks in Squid, which has a
standard configuration.::

	 refresh_pattern ^http://images.   3600   20%     7200
	 refresh_pattern -i (/cgi-bin/|\?)    0    0%        0
	 refresh_pattern -i (/\.jpg)       1800   10%     3600 override-expire 
	 refresh_pattern .                    0   20%     4320

If you are familiar with squid that probably made sense to you. But
lets point out a few weaknesses with this model.

1) It's not intuitive. You can guess what the options mean, and you
   can (and should) document it in your configuration file.

2) Which rules have precedence? Does the last rule to match stick? Or
   the first? Or does Squid try to combine all the matching rules. I
   actually don't know. 

Enter VCL
---------

Now enter Varnish. Varnish takes your configuration file and
translates it to C code, then runs it through a compiler and loads
it. When requests come along varnish just executes the relevant
subroutines of the configuration at the relevant times.

Varnish will execute these subroutines of code at different stages of
its work. Since its code it's execute line by line and precedence
isn't a problem.

99% of all the changes you'll need to do will be done in two of these
subroutines.

vcl_recv
~~~~~~~~

vcl_recv (yes, we're skimpy with characters, it's Unix) is called at
the beginning of a request, after the complete request has been
received and parsed.  Its purpose is to decide whether or not to serve
the request, how to do it, and, if applicable, which backend to use.

In vcl_recv you can also alter the request, dropping cookies, rewrite
headers.


vcl_fetch
~~~~~~~~~

vcl_fetch is called *after* a document has been successfully retrieved
from the backend. Normal tasks her are to alter the response headers,
trigger ESI processing, try alternate backend servers in case the
request failed.

