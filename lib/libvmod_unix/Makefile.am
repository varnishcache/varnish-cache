#

AM_LDFLAGS  = $(AM_LT_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/bin/varnishd \
	-I$(top_builddir)/include

vmod_LTLIBRARIES =
EXTRA_DIST =
CLEANFILES =

libvmod_unix_la_SOURCES = \
	vmod_unix.c \
	cred_compat.h

# Use vmodtool.py generated automake boilerplate
include $(srcdir)/automake_boilerplate_unix.am
