# $Id$

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishhist

dist_man_MANS = varnishhist.1

varnishhist_SOURCES = varnishhist.c

varnishhist_LDADD = \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	-lm \
	${CURSES_LIBS} ${PTHREAD_LIBS}

%.1: ../../doc/sphinx/reference/%.rst
if HAVE_RST2MAN
	${RST2MAN} $< >$@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
