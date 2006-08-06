# $Id: Makefile.am 133 2006-04-06 09:38:00Z phk $

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishstat

dist_man_MANS = varnishstat.1

varnishstat_SOURCES = varnishstat.c

varnishstat_LDADD = \
	-lcurses \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la 
