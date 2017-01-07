#

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include

bin_PROGRAMS = varnishadm

varnishadm_SOURCES = varnishadm.c

varnishadm_CFLAGS = @LIBEDIT_CFLAGS@ \
	@SAN_CFLAGS@

varnishadm_LDADD = \
	$(top_builddir)/lib/libvarnish/libvarnish.a \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	${PTHREAD_LIBS} ${RT_LIBS} ${NET_LIBS} @LIBEDIT_LIBS@ ${LIBM} \
	@SAN_LDFLAGS@
