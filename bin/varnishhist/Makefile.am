# $Id: Makefile.am 767 2006-08-08 12:31:19Z des $

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishhist

# dist_man_MANS = varnishlog.1

varnishhist_SOURCES = varnishhist.c

varnishhist_CFLAGS = -include config.h

varnishhist_LDADD = \
	$(top_builddir)/lib/libcompat/libcompat.a \
	$(top_builddir)/lib/libvarnish/libvarnish.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	-lm -lcurses
