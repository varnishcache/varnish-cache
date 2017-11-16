#

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include

bin_PROGRAMS = varnishncsa

varnishncsa_SOURCES = \
	varnishncsa.c \
	varnishncsa_options.h \
	b64.h \
	b64.c

varnishncsa_CFLAGS = \
	@SAN_CFLAGS@

varnishncsa_LDADD = \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	@SAN_LDFLAGS@ \
	${RT_LIBS} ${LIBM}
