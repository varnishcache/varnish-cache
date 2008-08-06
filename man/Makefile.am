# $Id$

dist_man_MANS = vcl.7

vcl.7:	vcl.7so default.vcl
	soelim $< > $@
default.vcl:	../bin/varnishd/default.vcl
	sed -n '/vcl_recv/,$$w $@' $<
