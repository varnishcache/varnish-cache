# $Id$

INCLUDES = -I$(top_srcdir)/include @PCRE_CFLAGS@

lib_LTLIBRARIES = libvarnishapi.la

libvarnishapi_la_LDFLAGS = -version-info 1:0:0

libvarnishapi_la_SOURCES = \
	vsm_api.h \
	vsl_api.h \
	\
	../libvarnish/assert.c \
	../libvarnish/argv.c \
	../libvarnish/vcs_version.c \
	../libvarnish/version.c \
	../libvarnish/vin.c \
	../libvarnish/vmb.c \
	../libvarnish/vre.c \
	base64.c \
	vsm.c \
	vsl_arg.c \
	vsl.c \
	vsc.c

libvarnishapi_la_CFLAGS = \
	-DVARNISH_STATE_DIR='"${VARNISH_STATE_DIR}"'

libvarnishapi_la_LIBADD = @PCRE_LIBS@
