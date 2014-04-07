#

noinst_PROGRAMS = vsc2rst
vsc2rst_SOURCES = vsc2rst.c \
	$(top_srcdir)/include/tbl/vsc_fields.h

AM_CPPFLAGS = -I$(top_srcdir)/include

dist_man_MANS = \
	varnish-cli.7 \
	varnish-counters.7 \
	vcl.7 \
	vsl.7 \
	vsl-query.7 \
	varnishadm.1 \
	varnishd.1 \
	varnishhist.1 \
	varnishlog.1 \
	varnishncsa.1 \
	varnishstat.1 \
	varnishtest.1 \
	varnishtop.1 \
	vmod_directors.3 \
	vmod_std.3

MAINTAINERCLEANFILES = $(dist_man_MANS)

varnish-cli.7: $(top_srcdir)/doc/sphinx/reference/varnish-cli.rst
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/varnish-cli.rst $@

varnish-counters.7: vsc2rst
	./vsc2rst | ${RST2MAN} - $@

vcl.7: $(top_srcdir)/doc/sphinx/reference/vcl.rst \
	$(top_srcdir)/bin/varnishd/builtin.vcl
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/vcl.rst $@

vsl.7: $(top_srcdir)/doc/sphinx/reference/vsl.rst \
	$(top_srcdir)/lib/libvarnishapi/vsl-tags.rst
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/vsl.rst $@

vsl-query.7: $(top_srcdir)/doc/sphinx/reference/vsl-query.rst
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/vsl-query.rst $@

varnishadm.1: $(top_srcdir)/doc/sphinx/reference/varnishadm.rst
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/varnishadm.rst $@

varnishd.1: \
	$(top_srcdir)/doc/sphinx/reference/varnishd.rst \
	$(top_srcdir)/doc/sphinx/include/params.rst
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/varnishd.rst $@

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

varnishreplay.1: $(top_srcdir)/doc/sphinx/reference/varnishreplay.rst
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/varnishreplay.rst $@

# XXX add _options.rst and _synopsis.rst here when it's been _opt2rst'ed
varnishstat.1: $(top_srcdir)/doc/sphinx/reference/varnishstat.rst
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/varnishstat.rst $@

varnishtest.1: $(top_srcdir)/doc/sphinx/reference/varnishtest.rst
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/varnishtest.rst $@

varnishtop.1: \
	$(top_srcdir)/doc/sphinx/reference/varnishtop.rst \
	$(top_srcdir)/doc/sphinx/include/varnishtop_options.rst \
	$(top_srcdir)/doc/sphinx/include/varnishtop_synopsis.rst
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/varnishtop.rst $@

varnishhist.1: \
	$(top_srcdir)/doc/sphinx/reference/varnishhist.rst \
	$(top_srcdir)/doc/sphinx/include/varnishhist_options.rst \
	$(top_srcdir)/doc/sphinx/include/varnishhist_synopsis.rst
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/varnishhist.rst $@

vmod_std.3: $(top_srcdir)/lib/libvmod_std/vmod_std.man.rst
	${RST2MAN} $? $@

vmod_directors.3: $(top_srcdir)/lib/libvmod_directors/vmod_directors.man.rst
	${RST2MAN} $? $@
