#

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include

bin_PROGRAMS = varnishadm

varnishadm_SOURCES = \
	varnishadm.c \
	$(top_srcdir)/lib/libvarnish/vas.c \
	$(top_srcdir)/lib/libvarnish/vsa.c \
	$(top_srcdir)/lib/libvarnish/vtcp.c \
	$(top_srcdir)/lib/libvarnish/vss.c

varnishadm_CFLAGS = @LIBEDIT_CFLAGS@

varnishadm_LDADD = \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	${PTHREAD_LIBS} ${RT_LIBS} ${NET_LIBS} @LIBEDIT_LIBS@ ${LIBM}
