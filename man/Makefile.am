# $Id$

EXTRA_DIST = vcl.7so
DISTCLEANFILES = vcl.7 default.vcl

dist_man_MANS = vcl.7

vcl.7:	vcl.7so default.vcl Makefile
	soelim $(srcdir)/vcl.7so > $@
default.vcl:	$(top_srcdir)/bin/varnishd/default.vcl Makefile
	sed -n '/vcl_recv/,$$p' $(top_srcdir)/bin/varnishd/default.vcl > $@
