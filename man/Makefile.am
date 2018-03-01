#

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
	vtc.7 \
	varnishtop.1 \
	vmod_directors.3 \
	vmod_purge.3 \
	vmod_std.3 \
	vmod_vtc.3 \
	vmod_blob.3 \
	vmod_unix.3 \
	vmod_proxy.3

CLEANFILES = $(dist_man_MANS)

RST2ANY_FLAGS = --halt=2

varnish-cli.7: $(top_builddir)/doc/sphinx/reference/varnish-cli.rst
	${RST2MAN} $(RST2ANY_FLAGS) $(top_srcdir)/doc/sphinx/reference/varnish-cli.rst $@

varnish-counters.7: $(top_builddir)/doc/sphinx/reference/varnish-counters.rst
	${RST2MAN} $(RST2ANY_FLAGS) $(top_srcdir)/doc/sphinx/reference/varnish-counters.rst $@

vcl.7: $(top_builddir)/doc/sphinx/reference/vcl.rst \
	$(top_builddir)/bin/varnishd/builtin.vcl
	${RST2MAN} $(RST2ANY_FLAGS) $(top_srcdir)/doc/sphinx/reference/vcl.rst $@

vsl.7: $(top_builddir)/doc/sphinx/reference/vsl.rst \
	$(top_builddir)/doc/sphinx/include/vsl-tags.rst
	${RST2MAN} $(RST2ANY_FLAGS) $(top_srcdir)/doc/sphinx/reference/vsl.rst $@

vsl-query.7: $(top_builddir)/doc/sphinx/reference/vsl-query.rst
	${RST2MAN} $(RST2ANY_FLAGS) $(top_srcdir)/doc/sphinx/reference/vsl-query.rst $@

varnishadm.1: $(top_builddir)/doc/sphinx/reference/varnishadm.rst
	${RST2MAN} $(RST2ANY_FLAGS) $(top_srcdir)/doc/sphinx/reference/varnishadm.rst $@

varnishd.1: \
	$(top_builddir)/doc/sphinx/reference/varnishd.rst \
	$(top_builddir)/doc/sphinx/include/params.rst
	${RST2MAN} $(RST2ANY_FLAGS) $(top_srcdir)/doc/sphinx/reference/varnishd.rst $@

varnishncsa.1: \
	$(top_builddir)/doc/sphinx/reference/varnishncsa.rst \
	$(top_builddir)/doc/sphinx/include/varnishncsa_options.rst \
	$(top_builddir)/doc/sphinx/include/varnishncsa_synopsis.rst
	${RST2MAN} $(RST2ANY_FLAGS) $(top_srcdir)/doc/sphinx/reference/varnishncsa.rst $@

varnishlog.1: \
	$(top_builddir)/doc/sphinx/reference/varnishlog.rst \
	$(top_builddir)/doc/sphinx/include/varnishlog_options.rst \
	$(top_builddir)/doc/sphinx/include/varnishlog_synopsis.rst
	${RST2MAN} $(RST2ANY_FLAGS) $(top_srcdir)/doc/sphinx/reference/varnishlog.rst $@

varnishstat.1: $(top_builddir)/doc/sphinx/reference/varnishstat.rst \
	$(top_builddir)/doc/sphinx/include/varnishstat_options.rst \
	$(top_builddir)/doc/sphinx/include/varnishstat_synopsis.rst
	${RST2MAN} $(RST2ANY_FLAGS) $(top_srcdir)/doc/sphinx/reference/varnishstat.rst $@

varnishtest.1: $(top_builddir)/doc/sphinx/reference/varnishtest.rst
	${RST2MAN} $(RST2ANY_FLAGS) $(top_srcdir)/doc/sphinx/reference/varnishtest.rst $@

vtc.7: $(top_builddir)/doc/sphinx/reference/vtc.rst \
	$(top_builddir)/doc/sphinx/include/vtc-syntax.rst
	${RST2MAN} $(RST2ANY_FLAGS) $(top_srcdir)/doc/sphinx/reference/vtc.rst $@

varnishtop.1: \
	$(top_builddir)/doc/sphinx/reference/varnishtop.rst \
	$(top_builddir)/doc/sphinx/include/varnishtop_options.rst \
	$(top_builddir)/doc/sphinx/include/varnishtop_synopsis.rst
	${RST2MAN} $(RST2ANY_FLAGS) $(top_srcdir)/doc/sphinx/reference/varnishtop.rst $@

varnishhist.1: \
	$(top_builddir)/doc/sphinx/reference/varnishhist.rst \
	$(top_builddir)/doc/sphinx/include/varnishhist_options.rst \
	$(top_builddir)/doc/sphinx/include/varnishhist_synopsis.rst
	${RST2MAN} $(RST2ANY_FLAGS) $(top_srcdir)/doc/sphinx/reference/varnishhist.rst $@

vmod_directors.3: $(top_builddir)/lib/libvmod_directors/vmod_directors.man.rst
	${RST2MAN} $(RST2ANY_FLAGS) $? $@

vmod_purge.3: $(top_builddir)/lib/libvmod_purge/vmod_purge.man.rst
	${RST2MAN} $(RST2ANY_FLAGS) $? $@

vmod_std.3: $(top_builddir)/lib/libvmod_std/vmod_std.man.rst
	${RST2MAN} $(RST2ANY_FLAGS) $? $@

vmod_vtc.3: $(top_builddir)/lib/libvmod_vtc/vmod_vtc.man.rst
	${RST2MAN} $(RST2ANY_FLAGS) $? $@

vmod_blob.3: $(top_builddir)/lib/libvmod_blob/vmod_blob.man.rst
	${RST2MAN} $(RST2ANY_FLAGS) $? $@

vmod_unix.3: $(top_builddir)/lib/libvmod_unix/vmod_unix.man.rst
	${RST2MAN} $(RST2ANY_FLAGS) $? $@

vmod_proxy.3: $(top_builddir)/lib/libvmod_proxy/vmod_proxy.man.rst
	${RST2MAN} $(RST2ANY_FLAGS) $? $@

.NOPATH: $(dist_man_MANS)
