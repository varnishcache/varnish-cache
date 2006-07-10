# $Id: Makefile.am 347 2006-07-06 09:31:45Z des $

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishtop

# man_MANS = varnishtop.1

varnishtop_SOURCES = varnishtop.c

varnishtop_LDADD = \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	$(top_builddir)/lib/libsbuf/libsbuf.a \
	-lcurses
