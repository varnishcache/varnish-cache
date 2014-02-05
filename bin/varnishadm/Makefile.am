#

AM_CPPFLAGS = -I$(top_srcdir)/include

bin_PROGRAMS = varnishadm

varnishadm_SOURCES = \
	varnishadm.c \
	$(top_builddir)/lib/libvarnish/vas.c \
	$(top_builddir)/lib/libvarnish/vsa.c \
	$(top_builddir)/lib/libvarnish/vtcp.c \
	$(top_builddir)/lib/libvarnish/vss.c

varnishadm_CFLAGS = @LIBEDIT_CFLAGS@

varnishadm_LDADD = \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	${PTHREAD_LIBS} ${RT_LIBS} ${NET_LIBS} @LIBEDIT_LIBS@ ${LIBM}
