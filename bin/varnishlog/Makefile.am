#

AM_CPPFLAGS = -I$(top_srcdir)/include

bin_PROGRAMS = varnishlog

dist_man_MANS = varnishlog.1

varnishlog_SOURCES = \
	varnishlog.c \
	varnishlog_options.h \
	varnishlog_options.c \
	$(top_srcdir)/lib/libvarnishtools/vut.c \
	$(top_srcdir)/lib/libvarnish/vas.c \
	$(top_srcdir)/lib/libvarnish/flopen.c \
	$(top_srcdir)/lib/libvarnish/version.c \
	$(top_srcdir)/lib/libvarnish/vsb.c \
	$(top_srcdir)/lib/libvarnish/vpf.c \
	$(top_srcdir)/lib/libvarnish/vtim.c

varnishlog_LDADD = \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	${RT_LIBS} ${LIBM} ${PTHREAD_LIBS}

noinst_PROGRAMS = varnishlog_opt2rst
varnishlog_opt2rst_SOURCES = \
	varnishlog_options.h \
	varnishlog_options.c \
	$(top_srcdir)/lib/libvarnishtools/opt2rst.c

BUILT_SOURCES = varnishlog_options.rst varnishlog_synopsis.rst
EXTRA_DIST = $(BUILT_SOURCES)
MAINTAINERCLEANFILES = $(EXTRA_DIST)

varnishlog_options.rst:
	./varnishlog_opt2rst options > $@
varnishlog_synopsis.rst:
	./varnishlog_opt2rst synopsis > $@

if HAVE_RST2MAN
varnishlog_options.rst varnishlog_synopsis.rst: varnishlog_opt2rst
endif

varnishlog.1: \
	$(top_srcdir)/doc/sphinx/reference/varnishlog.rst \
	varnishlog_options.rst \
	varnishlog_synopsis.rst
if HAVE_RST2MAN
	${RST2MAN} $? $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
