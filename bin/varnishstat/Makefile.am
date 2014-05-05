#

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include

bin_PROGRAMS = varnishstat

varnishstat_SOURCES = \
	varnishstat.h \
	\
	varnishstat.c \
	varnishstat_curses.c \
	$(top_srcdir)/lib/libvarnish/vas.c \
	$(top_srcdir)/lib/libvarnish/version.c \
	$(top_srcdir)/lib/libvarnish/vtim.c

varnishstat_LDADD = \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	@CURSES_LIB@ ${RT_LIBS} ${LIBM} ${PTHREAD_LIBS}
