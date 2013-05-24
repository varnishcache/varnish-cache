#

AM_CPPFLAGS = -I$(top_srcdir)/include

bin_PROGRAMS = varnishlog

dist_man_MANS = varnishlog.1

varnishlog_SOURCES = \
	varnishlog.c \
	varnishlog_options.h \
	vut.c \
	vut.h \
	$(top_builddir)/lib/libvarnish/vas.c \
	$(top_builddir)/lib/libvarnish/flopen.c \
	$(top_builddir)/lib/libvarnish/version.c \
	$(top_builddir)/lib/libvarnish/vsb.c \
	$(top_builddir)/lib/libvarnish/vpf.c \
	$(top_builddir)/lib/libvarnish/vtim.c

varnishlog_LDADD = \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	${RT_LIBS} ${LIBM} ${PTHREAD_LIBS}

noinst_PROGRAMS = varnishlog_opt2rst
varnishlog_opt2rst_CFLAGS = -DOPT2RST_INC="varnishlog_options.h"
varnishlog_opt2rst_SOURCES = \
	opt2rst.c

BUILT_SOURCES = varnishlog_options.rst

EXTRA_DIST = varnishlog_options.rst

varnishlog_options.rst: varnishlog_opt2rst
	./varnishlog_opt2rst > $@

varnishlog.1: \
	$(top_srcdir)/doc/sphinx/reference/varnishlog.rst \
	varnishlog_options.rst
if HAVE_RST2MAN
	${RST2MAN} $< $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
