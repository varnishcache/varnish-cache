#

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include

bin_PROGRAMS = varnishstat varnishstat_help_gen

varnishstat_SOURCES = \
	varnishstat.h \
	varnishstat.c \
	varnishstat_bindings.h \
	varnishstat_curses.c \
	varnishstat_options.h

nodist_varnishstat_SOURCES = \
	varnishstat_curses_help.c

BUILT_SOURCES = $(nodist_varnishstat_SOURCES)
DISTCLEANFILES = $(nodist_varnishstat_SOURCES)

varnishstat_help_gen_SOURCES = \
	varnishstat_help_gen.c \
	varnishstat_bindings.h

varnishstat_curses_help.c: varnishstat_help_gen
	$(AM_V_GEN) ./varnishstat_help_gen >$@_
	@mv $@_ $@

varnishstat_CFLAGS = \
	@SAN_CFLAGS@

varnishstat_LDADD = \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	@SAN_LDFLAGS@ \
	@CURSES_LIBS@ ${RT_LIBS} ${LIBM} ${PTHREAD_LIBS}

varnishstat_help_gen_LDADD = \
	$(top_builddir)/lib/libvarnish/libvarnish.la \
	@SAN_LDFLAGS@
