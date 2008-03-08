# $Id$

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishreplay

dist_man_MANS = varnishreplay.1

varnishreplay_SOURCES = \
	varnishreplay.c

varnishreplay_LDADD = \
	$(top_builddir)/lib/libvarnish/libvarnish.la \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	${PTHREAD_LIBS}

