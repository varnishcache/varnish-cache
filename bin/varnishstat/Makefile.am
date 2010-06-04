# $Id$

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishstat

dist_man_MANS = varnishstat.1

varnishstat_SOURCES = \
	varnishstat.h \
	\
	varnishstat.c \
	varnishstat_curses.c

varnishstat_LDADD = \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	${CURSES_LIBS} ${RT_LIBS} ${PTHREAD_LIBS}
