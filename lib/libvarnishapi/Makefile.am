# $Id$

INCLUDES = -I$(top_srcdir)/include @PCRE_CFLAGS@

lib_LTLIBRARIES = libvarnishapi.la

libvarnishapi_la_LDFLAGS = -version-info 1:0:0

libvarnishapi_la_SOURCES = \
	../libvarnish/assert.c \
	../libvarnish/vre.c \
	base64.c \
	instance.c \
	shmlog.c

libvarnishapi_la_CFLAGS = \
	-DVARNISH_STATE_DIR='"${VARNISH_STATE_DIR}"'

libvarnishapi_la_LIBADD = @PCRE_LIBS@
