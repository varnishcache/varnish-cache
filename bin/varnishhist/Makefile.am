#

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include

bin_PROGRAMS = varnishhist

varnishhist_SOURCES = varnishhist.c \
	varnishhist_options.h \
	varnishhist_options.c \
	$(top_srcdir)/lib/libvarnish/vas.c \
	$(top_srcdir)/lib/libvarnish/version.c \
	$(top_srcdir)/lib/libvarnish/vtim.c

varnishhist_CFLAGS = \
	@SAN_CFLAGS@

varnishhist_LDADD = \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	-lm \
	@SAN_LDFLAGS@ \
	@CURSES_LIB@ ${RT_LIBS} ${PTHREAD_LIBS}
