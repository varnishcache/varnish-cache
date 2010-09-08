# $Id$

TESTS_ENVIRONMENT = ./varnishtest
TESTS = $(srcdir)/tests/*.vtc

DISTCLEANFILES = _.ok

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS =	varnishtest

if HAVE_RST2MAN
dist_man_MANS = varnishtest.1
endif

varnishtest_SOURCES = \
		vtc.c \
		vtc.h \
		vtc_client.c \
		vtc_http.c \
		vtc_log.c \
		vtc_sema.c \
		vtc_server.c \
		vtc_varnish.c

varnishtest_LDADD = \
		$(top_builddir)/lib/libvarnish/libvarnish.la \
		$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
		$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
		${LIBM} ${PTHREAD_LIBS}

varnishtest_CFLAGS = \
		-DTOP_SRCDIR='"${top_srcdir}"'

EXTRA_DIST = $(top_srcdir)/bin/varnishtest/tests/*.vtc \
	$(top_srcdir)/bin/varnishtest/tests/README

%.1: ../../doc/sphinx/reference/%.rst
if HAVE_RST2MAN
	${RST2MAN} $< >$@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
