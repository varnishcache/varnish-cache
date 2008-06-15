# $Id$

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS =	varnishtest

varnishtest_SOURCES = \
		vtc.c \
		vtc_client.c \
		vtc_http.c \
		vtc_server.c \
		vtc_stats.c \
		vtc_varnish.c \
		vtc_vcl.c

varnishtest_LDADD = \
		$(top_builddir)/lib/libvarnish/libvarnish.la \
		$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
		${PTHREAD_LIBS}
