# $Id$

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishhist

dist_man_MANS = varnishhist.1

varnishhist_SOURCES = varnishhist.c

varnishhist_LDADD = \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	-lm \
	${CURSES_LIBS} ${PTHREAD_LIBS}
