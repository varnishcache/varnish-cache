# $Id$

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS =	varnishtest

varnishtest_SOURCES = \
		vtc.c \
		vtc.h \
		vtc_client.c \
		vtc_http.c \
		vtc_log.c \
		vtc_server.c \
		vtc_stats.c \
		vtc_varnish.c

varnishtest_LDADD = \
		$(top_builddir)/lib/libvarnish/libvarnish.la \
		$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
		${PTHREAD_LIBS}

EXTRA_DIST = $(top_srcdir)/bin/varnishtest/tests/*.vtc \
	$(top_srcdir)/bin/varnishtest/tests/README
