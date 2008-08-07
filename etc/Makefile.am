# $Id$

EXTRA_DIST = zope-plone.vcl

dist_data_DATA = default.vcl

default.vcl:	$(top_srcdir)/bin/varnishd/default.vcl
	echo -e "This is a basic VCL configuration file for varnish.  See the vcl(7)\n\
man page for details on VCL syntax and semantics.\n\
\n\
Default backend definition.  Set this to point to your content\n\
server.\n\
\n\
backend default {\n\
	.host = "127.0.0.1";\n\
	.port = "8080";\n\
}\n\
\n\
Below is a commented-out copy of the default VCL logic.  If you\n\
redefine any of these subroutines, the built-in logic will be\n\
appended to your code.\n" > tmp.vcl
	sed -n '/vcl_recv/,$$p' $(top_srcdir)/bin/varnishd/default.vcl >> tmp.vcl
	sed 's/^\(.*\)$$/#\1/' tmp.vcl > default.vcl
	rm tmp.vcl
