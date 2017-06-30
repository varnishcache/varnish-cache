#

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include

bin_PROGRAMS = varnishreplay

varnishreplay_SOURCES = \
	varnishreplay.c \
	varnishreplay_options.h \
	varnishreplay_options.c \
	rsm.h \
	rsm.c \
	stat_cnt.h \
	stat_cnt.c \
	$(top_srcdir)/lib/libvarnishtools/vut.c \
	$(top_srcdir)/lib/libvarnish/vas.c \
	$(top_srcdir)/lib/libvarnish/flopen.c \
	$(top_srcdir)/lib/libvarnish/version.c \
	$(top_srcdir)/lib/libvarnish/vsb.c \
	$(top_srcdir)/lib/libvarnish/vpf.c \
	$(top_srcdir)/lib/libvarnish/vtim.c

varnishreplay_LDADD = \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	${RT_LIBS} ${LIBM} ${PTHREAD_LIBS}

noinst_PROGRAMS = varnishreplay_opt2rst
varnishreplay_opt2rst_SOURCES = \
	varnishreplay_options.h \
	varnishreplay_options.c \
	$(top_srcdir)/lib/libvarnishtools/opt2rst.c
