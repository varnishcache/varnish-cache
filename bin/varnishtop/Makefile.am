# $Id$

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishtop

dist_man_MANS = varnishtop.1

varnishtop_SOURCES = varnishtop.c

varnishtop_LDADD = \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	${CURSES_LIBS} ${PTHREAD_LIBS}
