#

if HAVE_RST2MAN
noinst_PROGRAMS = vsc2rst
endif
vsc2rst_SOURCES = vsc2rst.c \
	$(top_srcdir)/include/tbl/vsc_fields.h

AM_CPPFLAGS = -I$(top_srcdir)/include

dist_man_MANS = vcl.7 \
	varnish-cli.7 \
	varnish-counters.7 \
	vsl.7 \
	vsl-query.7 \
	varnishadm.1 \
	varnishd.1 \
	varnishlog.1 \
	varnishncsa.1 \
	varnishreplay.1 \
	varnishstat.1 \
	varnishtest.1 \
	varnishtop.1

MAINTAINERCLEANFILES = $(dist_man_MANS)

vcl.7: $(top_srcdir)/doc/sphinx/reference/vcl.rst \
	$(top_srcdir)/bin/varnishd/builtin.vcl
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/vcl.rst $@

varnish-cli.7: $(top_srcdir)/doc/sphinx/reference/varnish-cli.rst
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/varnish-cli.rst $@

varnish-counters.7: vsc2rst
	./vsc2rst | ${RST2MAN} - $@

vsl.7: $(top_srcdir)/doc/sphinx/reference/vsl.rst \
	$(top_srcdir)/lib/libvarnishapi/vsl-tags.rst
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/vsl.rst $@

vsl-query.7: $(top_srcdir)/doc/sphinx/reference/vsl-query.rst
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/vsl-query.rst $@

%.7: $(top_srcdir)/doc/sphinx/reference/%.rst
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/vsl-query.rst $@

%.1: $(top_srcdir)/doc/sphinx/reference/%.rst
	${RST2MAN} $< $@

varnishncsa.1: \
	$(top_srcdir)/doc/sphinx/reference/varnishncsa.rst \
	$(top_srcdir)/doc/sphinx/include/varnishncsa_options.rst \
	$(top_srcdir)/doc/sphinx/include/varnishncsa_synopsis.rst
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/varnishncsa.rst $@

varnishlog.1: \
	$(top_srcdir)/doc/sphinx/reference/varnishlog.rst \
	$(top_srcdir)/doc/sphinx/include/varnishlog_options.rst \
	$(top_srcdir)/doc/sphinx/include/varnishlog_synopsis.rst
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/varnishlog.rst $@

varnishtop.1: \
	$(top_srcdir)/doc/sphinx/reference/varnishtop.rst \
	$(top_srcdir)/doc/sphinx/include/varnishtop_options.rst \
	$(top_srcdir)/doc/sphinx/include/varnishtop_synopsis.rst
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/varnishtop.rst $@
