#

AM_CPPFLAGS = -I$(top_srcdir)/include

bin_PROGRAMS = varnishtop

varnishtop_SOURCES = varnishtop.c \
	varnishtop_options.h \
	varnishtop_options.c \
	$(top_srcdir)/lib/libvarnishtools/vut.c \
	$(top_srcdir)/lib/libvarnish/vas.c \
	$(top_srcdir)/lib/libvarnish/flopen.c \
	$(top_srcdir)/lib/libvarnish/version.c \
	$(top_srcdir)/lib/libvarnish/vpf.c \
	$(top_srcdir)/lib/libvarnish/vtim.c \
	$(top_srcdir)/lib/libvarnish/vsb.c


varnishtop_LDADD = \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	@CURSES_LIB@ ${RT_LIBS} ${LIBM} ${PTHREAD_LIBS}

noinst_PROGRAMS = varnishtop_opt2rst
varnishtop_opt2rst_SOURCES = \
	varnishtop_options.h \
	varnishtop_options.c \
	$(top_srcdir)/lib/libvarnishtools/opt2rst.c
