#

AM_LDFLAGS  = $(AM_LT_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/bin/varnishd \
	-I$(top_builddir)/include

vmod_LTLIBRARIES =
EXTRA_DIST =
CLEANFILES =

libvmod_blob_la_SOURCES = \
	vmod_blob.c \
	vmod_blob.h \
	id.c \
	base64.c \
	hex.h \
	hex.c \
	url.c \
	tbl_encodings.h \
	tbl_case.h

# Use vmodtool.py generated automake boilerplate
include $(srcdir)/automake_boilerplate_blob.am
