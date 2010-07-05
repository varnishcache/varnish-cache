# $Id$

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishstat

if HAVE_RST2MAN
dist_man_MANS = varnishstat.1
endif

varnishstat_SOURCES = \
	varnishstat.h \
	\
	varnishstat.c \
	varnishstat_curses.c

varnishstat_LDADD = \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	${CURSES_LIBS} ${RT_LIBS} ${PTHREAD_LIBS}

%.1: ../../doc/sphinx/reference/%.rst
if HAVE_RST2MAN
	${RST2MAN} $< >$@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
