#

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishstat

dist_man_MANS = varnishstat.1

varnishstat_SOURCES = \
	varnishstat.h \
	\
	varnishstat.c \
	varnishstat_curses.c \
	$(top_builddir)/lib/libvarnish/version.c

varnishstat_LDADD = \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	${CURSES_LIBS} ${RT_LIBS} ${PTHREAD_LIBS}

varnishstat.1: $(top_srcdir)/doc/sphinx/reference/varnishstat.rst
if HAVE_RST2MAN
	${RST2MAN} $? $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
