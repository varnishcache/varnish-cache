#

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include

bin_PROGRAMS = varnishstat

varnishstat_SOURCES = \
	varnishstat.h \
	varnishstat.c \
	varnishstat_curses.c \
	varnishstat_options.h

varnishstat_CFLAGS = \
	@SAN_CFLAGS@

varnishstat_LDADD = \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	@SAN_LDFLAGS@ \
	@CURSES_LIBS@ ${RT_LIBS} ${LIBM} ${PTHREAD_LIBS}
