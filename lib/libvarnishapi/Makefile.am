# $Id$

INCLUDES = -I$(top_srcdir)/include @PCRE_CFLAGS@

lib_LTLIBRARIES = libvarnishapi.la

libvarnishapi_la_LDFLAGS = -version-info 1:0:0

libvarnishapi_la_SOURCES = \
	vslapi.h \
	\
	../libvarnish/assert.c \
	../libvarnish/argv.c \
	../libvarnish/svn_version.c \
	../libvarnish/version.c \
	../libvarnish/vin.c \
	../libvarnish/vmb.c \
	../libvarnish/vre.c \
	base64.c \
	vsl.c \
	vsl_arg.c \
	vsl_log.c \
	vsl_stat.c

libvarnishapi_la_CFLAGS = \
	-DVARNISH_STATE_DIR='"${VARNISH_STATE_DIR}"'

libvarnishapi_la_LIBADD = @PCRE_LIBS@
