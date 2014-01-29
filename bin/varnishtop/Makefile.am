#

AM_CPPFLAGS = -I$(top_srcdir)/include

bin_PROGRAMS = varnishtop

dist_man_MANS = varnishtop.1

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

BUILT_SOURCES = varnishtop_options.rst varnishtop_synopsis.rst
EXTRA_DIST = $(BUILT_SOURCES)
MAINTAINERCLEANFILES = $(EXTRA_DIST)

varnishtop_options.rst:
	./varnishtop_opt2rst options > $@
varnishtop_synopsis.rst:
	./varnishtop_opt2rst synopsis > $@

if HAVE_RST2MAN
varnishtop_options.rst varnishtop_synopsis.rst: varnishtop_opt2rst
endif

varnishtop.1: \
	$(top_srcdir)/doc/sphinx/reference/varnishtop.rst \
	varnishtop_options.rst \
	varnishtop_synopsis.rst
if HAVE_RST2MAN
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/varnishtop.rst $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
