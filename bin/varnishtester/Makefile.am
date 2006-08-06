# $Id: Makefile.am 426 2006-07-11 12:31:44Z phk $

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishtester

# dist_man_MANS = varnishtester.1

varnishtester_SOURCES = \
	varnishtester.c

varnishtester_CFLAGS = -include config.h

varnishtester_LDADD = \
	$(top_builddir)/lib/libvarnish/libvarnish.la
