# Makefile for Sphinx documentation
#

# You can set these variables from the command line.
SPHINXOPTS    =
SPHINXBUILD   = $(SPHINX) -W -q -N
BUILDDIR      = build

ALLSPHINXOPTS   = -d $(BUILDDIR)/doctrees $(SPHINXOPTS) $(builddir)

.PHONY: help clean html linkcheck doctest

help:
	@echo "Please use \`make <target>' where <target> is one of"
	@echo "  html      to make standalone HTML files"
	@echo "  linkcheck to check all external links for integrity"

clean:
	-rm -rf $(BUILDDIR)/* $(CLEANFILES)

# use index.rst as an indicator if we have copied already
.PHONY: link_srcdir
link_srcdir: graphviz conf.py $(BUILT_SOURCES)
	if test "x$(srcdir)" != "x$(builddir)" && test ! -f index.rst; then \
		s=`cd $(srcdir) && pwd`; \
		for f in `cd $$s && find . -type f`; do \
			d=`dirname $$f`; \
			test -d $$d || mkdir -p $$d; \
			test -f $$f || ln -s $$s/$$f $$f; \
		done \
	fi

# workaround for make html called within doc/sphinx
.PHONY: graphviz
graphviz:
	cd ../graphviz && $(MAKE) html

sphinx_prereq: link_srcdir

all: link_srcdir

html: sphinx_prereq
	$(SPHINXBUILD) -b html $(ALLSPHINXOPTS) $(BUILDDIR)/html
	@echo
	@echo "Build finished. The HTML pages are in $(subdir)/$(BUILDDIR)/html."

linkcheck: sphinx_prereq
	$(SPHINXBUILD) -b linkcheck $(ALLSPHINXOPTS) $(BUILDDIR)/linkcheck
	@echo
	@echo "Link check complete; look for any errors in the above output " \
	      "or in $(subdir)/$(BUILDDIR)/linkcheck/output.txt."

EXTRA_DIST = \
	conf.py \
	dev-guide \
	glossary \
	include \
	index.rst \
	installation \
	phk \
	tutorial \
	reference \
	users-guide \
	vcl-design-patterns \
	vtc-syntax.py \
	whats-new

dist-hook:
	$(MAKE) html
	rm -rf $(BUILDDIR)/doctrees
	cp -r $(BUILDDIR)/html $(distdir)/../
	@ # Remove build artifacts.
	rm $(distdir)/../html/.buildinfo $(distdir)/../html/*.inv

distclean-local:
	rm -rf $(BUILDDIR)

include/cli.rst: $(top_builddir)/bin/varnishd/varnishd
	$(top_builddir)/bin/varnishd/varnishd -x cli > ${@}_
	mv -f ${@}_ ${@}
BUILT_SOURCES = include/cli.rst

include/params.rst: $(top_builddir)/bin/varnishd/varnishd
	$(top_builddir)/bin/varnishd/varnishd -x parameter > ${@}_
	mv -f ${@}_ ${@}
BUILT_SOURCES += include/params.rst

include/options.rst: $(top_builddir)/bin/varnishd/varnishd
	$(top_builddir)/bin/varnishd/varnishd -x options > ${@}_
	mv -f ${@}_ ${@}
BUILT_SOURCES += include/options.rst

include/counters.rst:
	ln -s $(abs_top_builddir)/lib/libvsc/counters.rst $@
BUILT_SOURCES += include/counters.rst

include/varnishncsa_options.rst: $(top_builddir)/bin/varnishncsa/varnishncsa
	$(top_builddir)/bin/varnishncsa/varnishncsa --options > ${@}_
	mv -f ${@}_ ${@}
include/varnishncsa_synopsis.rst: $(top_builddir)/bin/varnishncsa/varnishncsa
	$(top_builddir)/bin/varnishncsa/varnishncsa --synopsis > ${@}_
	mv -f ${@}_ ${@}
BUILT_SOURCES += include/varnishncsa_options.rst \
	include/varnishncsa_synopsis.rst

include/varnishlog_options.rst: $(top_builddir)/bin/varnishlog/varnishlog
	$(top_builddir)/bin/varnishlog/varnishlog --options > ${@}_
	mv -f ${@}_ ${@}
include/varnishlog_synopsis.rst: $(top_builddir)/bin/varnishlog/varnishlog
	$(top_builddir)/bin/varnishlog/varnishlog --synopsis > ${@}_
	mv -f ${@}_ ${@}
BUILT_SOURCES += include/varnishlog_options.rst \
	include/varnishlog_synopsis.rst

include/varnishtop_options.rst: $(top_builddir)/bin/varnishtop/varnishtop
	$(top_builddir)/bin/varnishtop/varnishtop --options > ${@}_
	mv -f ${@}_ ${@}
include/varnishtop_synopsis.rst: $(top_builddir)/bin/varnishtop/varnishtop
	$(top_builddir)/bin/varnishtop/varnishtop --synopsis > ${@}_
	mv -f ${@}_ ${@}
BUILT_SOURCES += include/varnishtop_options.rst \
	 include/varnishtop_synopsis.rst

include/varnishhist_options.rst: $(top_builddir)/bin/varnishhist/varnishhist
	$(top_builddir)/bin/varnishhist/varnishhist --options > ${@}_
	mv -f ${@}_ ${@}
include/varnishhist_synopsis.rst: $(top_builddir)/bin/varnishhist/varnishhist
	$(top_builddir)/bin/varnishhist/varnishhist --synopsis > ${@}_
	mv -f ${@}_ ${@}
BUILT_SOURCES += include/varnishhist_options.rst \
	 include/varnishhist_synopsis.rst

include/varnishstat_options.rst: $(top_builddir)/bin/varnishstat/varnishstat
	$(top_builddir)/bin/varnishstat/varnishstat --options > ${@}_
	mv -f ${@}_ ${@}
include/varnishstat_synopsis.rst: $(top_builddir)/bin/varnishstat/varnishstat
	$(top_builddir)/bin/varnishstat/varnishstat --synopsis > ${@}_
	mv -f ${@}_ ${@}
include/varnishstat_bindings.rst: $(top_builddir)/bin/varnishstat/varnishstat
	$(top_builddir)/bin/varnishstat/varnishstat --bindings > ${@}_
	mv -f ${@}_ ${@}
BUILT_SOURCES += include/varnishstat_options.rst \
	 include/varnishstat_synopsis.rst \
	 include/varnishstat_bindings.rst

include/vsl-tags.rst: $(top_builddir)/lib/libvarnishapi/vsl2rst
	$(top_builddir)/lib/libvarnishapi/vsl2rst > ${@}_
	mv -f ${@}_ ${@}
BUILT_SOURCES += include/vsl-tags.rst

VTCSYN_SRC = \
	$(top_srcdir)/bin/varnishtest/vtc.c \
	$(top_srcdir)/bin/varnishtest/vtc_barrier.c \
	$(top_srcdir)/bin/varnishtest/vtc_haproxy.c \
	$(top_srcdir)/bin/varnishtest/vtc_http.c \
	$(top_srcdir)/bin/varnishtest/vtc_http2.c \
	$(top_srcdir)/bin/varnishtest/vtc_logexp.c \
	$(top_srcdir)/bin/varnishtest/vtc_misc.c \
	$(top_srcdir)/bin/varnishtest/vtc_process.c \
	$(top_srcdir)/bin/varnishtest/vtc_syslog.c \
	$(top_srcdir)/bin/varnishtest/vtc_tunnel.c \
	$(top_srcdir)/bin/varnishtest/vtc_varnish.c \
	$(top_srcdir)/bin/varnishtest/vtc_vsm.c
include/vtc-syntax.rst: $(srcdir)/vtc-syntax.py $(VTCSYN_SRC)
	$(AM_V_GEN) $(PYTHON) $(srcdir)/vtc-syntax.py $(VTCSYN_SRC) > ${@}_
	@mv -f ${@}_ ${@}
BUILT_SOURCES += include/vtc-syntax.rst

# XXX copy/paste rules need some TLC

include/vmod_std.generated.rst: $(top_builddir)/vmod/vmod_std.rst
	cp $(top_builddir)/vmod/vmod_std.rst $@
BUILT_SOURCES += include/vmod_std.generated.rst

include/vmod_directors.generated.rst: $(top_builddir)/vmod/vmod_directors.rst
	cp $(top_builddir)/vmod/vmod_directors.rst $@
BUILT_SOURCES += include/vmod_directors.generated.rst

include/vmod_purge.generated.rst: $(top_builddir)/vmod/vmod_purge.rst
	cp $(top_builddir)/vmod/vmod_purge.rst $@
BUILT_SOURCES += include/vmod_purge.generated.rst

include/vmod_vtc.generated.rst: $(top_builddir)/vmod/vmod_vtc.rst
	cp $(top_builddir)/vmod/vmod_vtc.rst $@
BUILT_SOURCES += include/vmod_vtc.generated.rst

include/vmod_blob.generated.rst: $(top_builddir)/vmod/vmod_blob.rst
	cp $(top_builddir)/vmod/vmod_blob.rst $@
BUILT_SOURCES += include/vmod_blob.generated.rst

include/vmod_cookie.generated.rst: $(top_builddir)/vmod/vmod_cookie.rst
	cp $(top_builddir)/vmod/vmod_cookie.rst $@
BUILT_SOURCES += include/vmod_cookie.generated.rst

include/vmod_h2.generated.rst: $(top_builddir)/vmod/vmod_h2.rst
	cp $(top_builddir)/vmod/vmod_h2.rst $@
BUILT_SOURCES += include/vmod_h2.generated.rst

include/vmod_unix.generated.rst: $(top_builddir)/vmod/vmod_unix.rst
	cp $(top_builddir)/vmod/vmod_unix.rst $@
BUILT_SOURCES += include/vmod_unix.generated.rst

include/vmod_proxy.generated.rst: $(top_builddir)/vmod/vmod_proxy.rst
	cp $(top_builddir)/vmod/vmod_proxy.rst $@
BUILT_SOURCES += include/vmod_proxy.generated.rst

EXTRA_DIST += $(BUILT_SOURCES)
CLEANFILES = $(BUILT_SOURCES)

.NOPATH: $(BUILT_SOURCES)
