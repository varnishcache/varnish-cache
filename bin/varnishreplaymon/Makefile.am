#

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include

bin_PROGRAMS = varnishreplaymon

varnishreplaymon_SOURCES = \
	varnishreplaymon.h \
	varnishreplaymon.c \
	varnishreplaymon_curses.c \
	../varnishreplay/rsm.c \
	$(top_builddir)/lib/libvarnish/version.c

varnishreplaymon_LDADD = \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	$(top_builddir)/lib/libvarnish/libvarnish.la \
	@CURSES_LIB@ ${RT_LIBS} ${PTHREAD_LIBS}
