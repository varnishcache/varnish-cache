# $Id$

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishadm

dist_man_MANS = varnishadm.1

varnishadm_SOURCES = \
	varnishadm.c

varnishadm_LDADD = \
	$(top_builddir)/lib/libvarnish/libvarnish.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	${PTHREAD_LIBS} ${NET_LIBS}
