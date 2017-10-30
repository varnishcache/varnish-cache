#

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include

bin_PROGRAMS = varnishhist

varnishhist_SOURCES = \
	varnishhist.c \
	varnishhist_options.h \
	varnishhist_profiles.h

varnishhist_CFLAGS = \
	@SAN_CFLAGS@

varnishhist_LDADD = \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	-lm \
	@SAN_LDFLAGS@ \
	@CURSES_LIBS@ ${RT_LIBS} ${PTHREAD_LIBS}
