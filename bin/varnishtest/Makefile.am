#

VTC_LOG_COMPILER = ./varnishtest -v -i
TEST_EXTENSIONS = .vtc
TESTS = @VTC_TESTS@

# Make sure we run check-local first
check: check-am check-local
check-am: check-local
# See if list of checks have changed, recheck
check-local:
	if [ "$$(cd $(srcdir) && echo tests/*.vtc)" != "@VTC_TESTS@" ]; then \
		cd $(top_builddir) && ./config.status --recheck ; \
	fi

DISTCLEANFILES = _.ok

AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/lib/libvgz

bin_PROGRAMS =	varnishtest

dist_man_MANS = varnishtest.1

varnishtest_SOURCES = \
		vtc.c \
		vtc.h \
		vtc_client.c \
		vtc_http.c \
		vtc_main.c \
		vtc_log.c \
		vtc_sema.c \
		vtc_server.c \
		vtc_varnish.c \
		vtc_logexp.c

varnishtest_LDADD = \
		$(top_builddir)/lib/libvarnish/libvarnish.la \
		$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
		$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
		$(top_builddir)/lib/libvgz/libvgz.la \
		${LIBM} ${PTHREAD_LIBS}

varnishtest_CFLAGS = \
		-DTOP_BUILDDIR='"${top_builddir}"'

EXTRA_DIST = $(top_srcdir)/bin/varnishtest/tests/*.vtc \
	$(top_srcdir)/bin/varnishtest/tests/README

varnishtest.1: $(top_srcdir)/doc/sphinx/reference/varnishtest.rst
if HAVE_RST2MAN
	${RST2MAN} $? $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
