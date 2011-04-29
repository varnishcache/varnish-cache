#

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishsizes

dist_man_MANS = varnishsizes.1

varnishsizes_SOURCES = varnishsizes.c

varnishsizes_LDADD = \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	-lm \
	${CURSES_LIBS} ${PTHREAD_LIBS}

varnishsizes.1: $(top_srcdir)/doc/sphinx/reference/varnishsizes.rst
if HAVE_RST2MAN
	${RST2MAN} $? $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
