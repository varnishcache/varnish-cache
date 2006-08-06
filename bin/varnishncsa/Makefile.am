# $Id$

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishncsa

dist_man_MANS = varnishncsa.1

varnishncsa_SOURCES = varnishncsa.c

varnishncsa_CFLAGS = -include config.h

varnishncsa_LDADD = \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	$(top_builddir)/lib/libsbuf/libsbuf.a 
