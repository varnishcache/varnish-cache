#

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include

bin_PROGRAMS = varnishtop

varnishtop_SOURCES = \
	varnishtop.c \
	varnishtop_options.h


varnishtop_CFLAGS = \
	@SAN_CFLAGS@

varnishtop_LDADD = \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	@SAN_LDFLAGS@ \
	@CURSES_LIBS@ ${RT_LIBS} ${LIBM} ${PTHREAD_LIBS}
