# $Id$

INCLUDES = -I$(top_srcdir)/include

lib_LTLIBRARIES = libvarnishapi.la

libvarnishapi_la_SOURCES = \
	base64.c \
	instance.c \
	shmlog.c

libvarnishapi_la_CFLAGS = \
	-DVARNISH_STATE_DIR='"${VARNISH_STATE_DIR}"'
