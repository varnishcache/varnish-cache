#

if HAVE_RST2MAN
noinst_PROGRAMS = vsc2rst
endif
vsc2rst_SOURCES = vsc2rst.c \
	$(top_srcdir)/include/tbl/vsc_fields.h

AM_CPPFLAGS = -I$(top_srcdir)/include

dist_man_MANS = vcl.7 varnish-cli.7 varnish-counters.7 vsl.7 vsl-query.7 varnishd.1
MAINTAINERCLEANFILES = $(dist_man_MANS)

vcl.7: $(top_srcdir)/doc/sphinx/reference/vcl.rst \
	$(top_srcdir)/bin/varnishd/builtin.vcl
if HAVE_RST2MAN
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/vcl.rst $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif

varnish-cli.7: $(top_srcdir)/doc/sphinx/reference/varnish-cli.rst
if HAVE_RST2MAN
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/varnish-cli.rst $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif

if HAVE_RST2MAN
varnish-counters.7: vsc2rst
	./vsc2rst | ${RST2MAN} - $@
else
varnish-counters.7: $(top_srcdir)/include/tbl/vsc_fields.h
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif

vsl.7: $(top_srcdir)/doc/sphinx/reference/vsl.rst \
	$(top_srcdir)/lib/libvarnishapi/vsl-tags.rst
if HAVE_RST2MAN
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/vsl.rst $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif

vsl-query.7: $(top_srcdir)/doc/sphinx/reference/vsl-query.rst
if HAVE_RST2MAN
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/vsl-query.rst $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif

varnishd.1: $(top_srcdir)/doc/sphinx/reference/varnishd.rst
if HAVE_RST2MAN
	${RST2MAN} $^ $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
