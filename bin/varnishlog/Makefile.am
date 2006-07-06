# $Id$

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishlog

man_MANS = varnishlog.1

varnishlog_SOURCES = varnishlog.c

varnishlog_LDADD = \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	$(top_builddir)/lib/libsbuf/libsbuf.a 
