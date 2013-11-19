#

AM_CPPFLAGS = -I$(top_srcdir)/include

bin_PROGRAMS = varnishncsa

dist_man_MANS = varnishncsa.1

varnishncsa_SOURCES = \
	varnishncsa.c \
	varnishncsa_options.h \
	varnishncsa_options.c \
	base64.h \
	base64.c \
	$(top_srcdir)/lib/libvarnishtools/vut.c \
	$(top_srcdir)/lib/libvarnish/vas.c \
	$(top_srcdir)/lib/libvarnish/flopen.c \
	$(top_srcdir)/lib/libvarnish/version.c \
	$(top_srcdir)/lib/libvarnish/vpf.c \
	$(top_srcdir)/lib/libvarnish/vtim.c \
	$(top_srcdir)/lib/libvarnish/vsb.c

varnishncsa_LDADD = \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	${RT_LIBS} ${LIBM}

noinst_PROGRAMS = varnishncsa_opt2rst
varnishncsa_opt2rst_SOURCES = \
	varnishncsa_options.h \
	varnishncsa_options.c \
	$(top_srcdir)/lib/libvarnishtools/opt2rst.c

BUILT_SOURCES = varnishncsa_options.rst varnishncsa_synopsis.rst
EXTRA_DIST = $(BUILT_SOURCES)
MAINTAINERCLEANFILES = $(EXTRA_DIST)

varnishncsa_options.rst:
	./varnishncsa_opt2rst options > $@
varnishncsa_synopsis.rst:
	./varnishncsa_opt2rst synopsis > $@

if HAVE_RST2MAN
varnishncsa_options.rst varnishncsa_synopsis.rst: varnishncsa_opt2rst
endif

varnishncsa.1: \
	$(top_srcdir)/doc/sphinx/reference/varnishncsa.rst \
	varnishncsa_options.rst \
	varnishncsa_synopsis.rst
if HAVE_RST2MAN
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/varnishncsa.rst $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
