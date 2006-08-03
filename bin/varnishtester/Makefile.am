# $Id: Makefile.am 426 2006-07-11 12:31:44Z phk $

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishtester

# man_MANS = varnishd.1

varnishtester_SOURCES = \
	varnishtester.c

# varnishd_LDFLAGS = -export-dynamic 

varnishtester_LDADD = \
	$(top_builddir)/lib/libvarnish/libvarnish.la
