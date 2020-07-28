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

# sphinx does not support include paths or anything comparable:
# link source tree files for an out-of-tree build
rstlinks = $(abs_builddir)/doc/sphinx/index.rst
$(rstlinks):
	s=`realpath $(srcdir)`; \
	for f in `cd $$s && find . -type f`; do \
		d=`dirname $$f`; \
		test -d $$d || mkdir -p $$d; \
		test -f $$f || ln -s $$s/$$f $$f; \
	done

BUILT_SOURCES = $(rstlinks)

# work around for make html called within doc/sphinx
.PHONY: graphviz
graphviz:
	cd ../graphviz && $(MAKE) html

sphinx_prereq: graphviz conf.py

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
	mv ${@}_ ${@}
RST_BUILT = include/cli.rst

include/params.rst: $(top_builddir)/bin/varnishd/varnishd
	$(top_builddir)/bin/varnishd/varnishd -x parameter > ${@}_
	mv ${@}_ ${@}
RST_BUILT += include/params.rst

COUNTERS = \
	$(top_srcdir)/bin/varnishd/VSC_main.vsc \
	$(top_srcdir)/bin/varnishd/VSC_mgt.vsc \
	$(top_srcdir)/bin/varnishd/VSC_mempool.vsc \
	$(top_srcdir)/bin/varnishd/VSC_sma.vsc \
	$(top_srcdir)/bin/varnishd/VSC_smu.vsc \
	$(top_srcdir)/bin/varnishd/VSC_smf.vsc \
	$(top_srcdir)/bin/varnishd/VSC_vbe.vsc \
	$(top_srcdir)/bin/varnishd/VSC_lck.vsc

include/counters.rst: $(top_srcdir)/lib/libvcc/vsctool.py $(COUNTERS)
	echo -n '' > ${@}_
	for i in $(COUNTERS); do \
		$(PYTHON) $(top_srcdir)/lib/libvcc/vsctool.py -r $$i >> ${@}_ ; \
	done
	mv ${@}_ ${@}

RST_BUILT += include/counters.rst

include/varnishncsa_options.rst: $(top_builddir)/bin/varnishncsa/varnishncsa
	$(top_builddir)/bin/varnishncsa/varnishncsa --options > ${@}_
	mv ${@}_ ${@}
include/varnishncsa_synopsis.rst: $(top_builddir)/bin/varnishncsa/varnishncsa
	$(top_builddir)/bin/varnishncsa/varnishncsa --synopsis > ${@}_
	mv ${@}_ ${@}
RST_BUILT += include/varnishncsa_options.rst \
	include/varnishncsa_synopsis.rst

include/varnishlog_options.rst: $(top_builddir)/bin/varnishlog/varnishlog
	$(top_builddir)/bin/varnishlog/varnishlog --options > ${@}_
	mv ${@}_ ${@}
include/varnishlog_synopsis.rst: $(top_builddir)/bin/varnishlog/varnishlog
	$(top_builddir)/bin/varnishlog/varnishlog --synopsis > ${@}_
	mv ${@}_ ${@}
RST_BUILT += include/varnishlog_options.rst \
	include/varnishlog_synopsis.rst

include/varnishtop_options.rst: $(top_builddir)/bin/varnishtop/varnishtop
	$(top_builddir)/bin/varnishtop/varnishtop --options > ${@}_
	mv ${@}_ ${@}
include/varnishtop_synopsis.rst: $(top_builddir)/bin/varnishtop/varnishtop
	$(top_builddir)/bin/varnishtop/varnishtop --synopsis > ${@}_
	mv ${@}_ ${@}
RST_BUILT += include/varnishtop_options.rst \
	 include/varnishtop_synopsis.rst

include/varnishhist_options.rst: $(top_builddir)/bin/varnishhist/varnishhist
	$(top_builddir)/bin/varnishhist/varnishhist --options > ${@}_
	mv ${@}_ ${@}
include/varnishhist_synopsis.rst: $(top_builddir)/bin/varnishhist/varnishhist
	$(top_builddir)/bin/varnishhist/varnishhist --synopsis > ${@}_
	mv ${@}_ ${@}
RST_BUILT += include/varnishhist_options.rst \
	 include/varnishhist_synopsis.rst

include/varnishstat_options.rst: $(top_builddir)/bin/varnishstat/varnishstat
	$(top_builddir)/bin/varnishstat/varnishstat --options > ${@}_
	mv ${@}_ ${@}
include/varnishstat_synopsis.rst: $(top_builddir)/bin/varnishstat/varnishstat
	$(top_builddir)/bin/varnishstat/varnishstat --synopsis > ${@}_
	mv ${@}_ ${@}
include/varnishstat_bindings.rst: $(top_builddir)/bin/varnishstat/varnishstat
	$(top_builddir)/bin/varnishstat/varnishstat --bindings > ${@}_
	mv ${@}_ ${@}
RST_BUILT += include/varnishstat_options.rst \
	 include/varnishstat_synopsis.rst \
	 include/varnishstat_bindings.rst

include/vsl-tags.rst: $(top_builddir)/lib/libvarnishapi/vsl2rst
	$(top_builddir)/lib/libvarnishapi/vsl2rst > ${@}_
	mv ${@}_ ${@}
RST_BUILT += include/vsl-tags.rst

VTCSYN_SRC = $(top_srcdir)/bin/varnishtest/vtc.c \
	     $(top_srcdir)/bin/varnishtest/vtc_barrier.c \
	     $(top_srcdir)/bin/varnishtest/vtc_haproxy.c \
	     $(top_srcdir)/bin/varnishtest/vtc_http.c \
	     $(top_srcdir)/bin/varnishtest/vtc_http2.c \
	     $(top_srcdir)/bin/varnishtest/vtc_logexp.c \
	     $(top_srcdir)/bin/varnishtest/vtc_misc.c \
	     $(top_srcdir)/bin/varnishtest/vtc_process.c \
	     $(top_srcdir)/bin/varnishtest/vtc_syslog.c \
	     $(top_srcdir)/bin/varnishtest/vtc_varnish.c
include/vtc-syntax.rst: vtc-syntax.py $(VTCSYN_SRC)
	$(AM_V_GEN) $(PYTHON) $(top_srcdir)/doc/sphinx/vtc-syntax.py $(VTCSYN_SRC) > ${@}_
	@mv ${@}_ ${@}
RST_BUILT += include/vtc-syntax.rst

# XXX copy/paste rules need some TLC

include/vmod_std.generated.rst: $(top_builddir)/lib/libvmod_std/vmod_std.rst
	cp $(top_builddir)/lib/libvmod_std/vmod_std.rst $@
RST_BUILT += include/vmod_std.generated.rst

include/vmod_directors.generated.rst: $(top_builddir)/lib/libvmod_directors/vmod_directors.rst
	cp $(top_builddir)/lib/libvmod_directors/vmod_directors.rst $@
RST_BUILT += include/vmod_directors.generated.rst

include/vmod_purge.generated.rst: $(top_builddir)/lib/libvmod_purge/vmod_purge.rst
	cp $(top_builddir)/lib/libvmod_purge/vmod_purge.rst $@
RST_BUILT += include/vmod_purge.generated.rst

include/vmod_vtc.generated.rst: $(top_builddir)/lib/libvmod_vtc/vmod_vtc.rst
	cp $(top_builddir)/lib/libvmod_vtc/vmod_vtc.rst $@
RST_BUILT += include/vmod_vtc.generated.rst

include/vmod_blob.generated.rst: $(top_builddir)/lib/libvmod_blob/vmod_blob.rst
	cp $(top_builddir)/lib/libvmod_blob/vmod_blob.rst $@
RST_BUILT += include/vmod_blob.generated.rst

include/vmod_cookie.generated.rst: $(top_builddir)/lib/libvmod_cookie/vmod_cookie.rst
	cp $(top_builddir)/lib/libvmod_cookie/vmod_cookie.rst $@
RST_BUILT += include/vmod_cookie.generated.rst

include/vmod_unix.generated.rst: $(top_builddir)/lib/libvmod_unix/vmod_unix.rst
	cp $(top_builddir)/lib/libvmod_unix/vmod_unix.rst $@
RST_BUILT += include/vmod_unix.generated.rst

include/vmod_proxy.generated.rst: $(top_builddir)/lib/libvmod_proxy/vmod_proxy.rst
	cp $(top_builddir)/lib/libvmod_proxy/vmod_proxy.rst $@
RST_BUILT += include/vmod_proxy.generated.rst

EXTRA_DIST += $(RST_BUILT)
BUILT_SOURCES += $(RST_BUILT)
CLEANFILES = $(RST_BUILT)
$(RST_BUILT): $(rstlinks)

.NOPATH: $(BUILT_SOURCES)
