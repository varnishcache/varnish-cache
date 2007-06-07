# $Id$

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishadm

dist_man_MANS = varnishadm.1

varnishadm_SOURCES = \
	varnishadm.c
	
varnishadm_CFLAGS = -include config.h

varnishadm_LDADD = \
	$(top_builddir)/lib/libcompat/libcompat.a \
	$(top_builddir)/lib/libvarnish/libvarnish.la
	
