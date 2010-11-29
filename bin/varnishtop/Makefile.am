# $Id$

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishtop

dist_man_MANS = varnishtop.1

varnishtop_SOURCES = varnishtop.c

varnishtop_LDADD = \
	$(top_builddir)/lib/libvarnish/libvarnish.la \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	${CURSES_LIBS} ${PTHREAD_LIBS}

varnishtop.1: $(top_srcdir)/doc/sphinx/reference/varnishtop.rst
if HAVE_RST2MAN
	${RST2MAN} $? $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
