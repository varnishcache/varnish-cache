#

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include

bin_PROGRAMS = varnishlog

varnishlog_SOURCES = \
	varnishlog.c \
	varnishlog_options.h

varnishlog_CFLAGS = \
	@SAN_CFLAGS@

varnishlog_LDADD = \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	@SAN_LDFLAGS@ \
	${RT_LIBS} ${LIBM} ${PTHREAD_LIBS}
