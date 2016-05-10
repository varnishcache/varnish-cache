#

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include

bin_PROGRAMS = varnishtop

varnishtop_SOURCES = varnishtop.c \
	varnishtop_options.h \
	varnishtop_options.c \
	$(top_srcdir)/lib/libvarnish/vas.c \
	$(top_srcdir)/lib/libvarnish/version.c \
	$(top_srcdir)/lib/libvarnish/vtim.c \
	$(top_srcdir)/lib/libvarnish/vsb.c


varnishtop_CFLAGS = \
	@SAN_CFLAGS@

varnishtop_LDADD = \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	@SAN_LDFLAGS@ \
	@CURSES_LIB@ ${RT_LIBS} ${LIBM} ${PTHREAD_LIBS}
