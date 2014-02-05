#

AM_CPPFLAGS = -I$(top_srcdir)/include

bin_PROGRAMS = varnishreplay

varnishreplay_SOURCES = \
	varnishreplay.c \
	$(top_builddir)/lib/libvarnish/vas.c \
	$(top_builddir)/lib/libvarnish/vtcp.c \
	$(top_builddir)/lib/libvarnish/vss.c

varnishreplay_LDADD = \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	${RT_LIBS} ${PTHREAD_LIBS} ${NET_LIBS} ${LIBM}
