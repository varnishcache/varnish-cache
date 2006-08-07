# $Id$

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishlog

dist_man_MANS = varnishlog.1

varnishlog_SOURCES = varnishlog.c

varnishlog_CFLAGS = -include config.h

varnishlog_LDADD = \
	$(top_builddir)/lib/libvarnish/libvarnish.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la
