# $Id$

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishreplay

dist_man_MANS = varnishreplay.1

varnishreplay_SOURCES = \
	varnishreplay.c
	
varnishreplay_CFLAGS = -include config.h

varnishreplay_LDADD = \
	$(top_builddir)/lib/libvarnish/libvarnish.la \
	$(top_builddir)/lib/libcompat/libcompat.a \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	${PTHREAD_LIBS}

