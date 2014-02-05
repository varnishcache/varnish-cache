#

AM_CPPFLAGS = -I$(top_srcdir)/include

bin_PROGRAMS = varnishhist

varnishhist_SOURCES = varnishhist.c \
	$(top_builddir)/lib/libvarnish/vas.c \
	$(top_builddir)/lib/libvarnish/version.c

varnishhist_LDADD = \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	-lm \
	@CURSES_LIB@ ${RT_LIBS} ${PTHREAD_LIBS}
