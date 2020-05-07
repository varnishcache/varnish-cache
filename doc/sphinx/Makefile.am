# Makefile for Sphinx documentation
#

# You can set these variables from the command line.
SPHINXOPTS    =
SPHINXBUILD   = $(SPHINX) -W -q -N
PAPER         = a4
BUILDDIR      = $(builddir)/build

ALLSPHINXOPTS   = -d $(BUILDDIR)/doctrees -D latex_elements.papersize=$(PAPER) $(SPHINXOPTS) $(srcdir)

.PHONY: help clean html dirhtml pickle json htmlhelp qthelp latex changes linkcheck doctest

help:
	@echo "Please use \`make <target>' where <target> is one of"
	@echo "  html      to make standalone HTML files"
	@echo "  dirhtml   to make HTML files named index.html in directories"
	@echo "  pickle    to make pickle files"
	@echo "  json      to make JSON files"
	@echo "  htmlhelp  to make HTML files and a HTML help project"
	@echo "  qthelp    to make HTML files and a qthelp project"
	@echo "  latex     to make LaTeX files, you can set PAPER=a4 or PAPER=letter"
	@echo "  changes   to make an overview of all changed/added/deprecated items"
	@echo "  linkcheck to check all external links for integrity"
	@echo "  doctest   to run all doctests embedded in the documentation (if enabled)"

clean:
	-rm -rf $(BUILDDIR)/* $(CLEANFILES)

# work around for make html called within doc/sphinx
.PHONY: graphviz
graphviz:
	cd ../graphviz && $(MAKE) html

sphinx_prereq: graphviz conf.py

html: sphinx_prereq
	$(SPHINXBUILD) -b html $(ALLSPHINXOPTS) $(BUILDDIR)/html
	@echo
	@echo "Build finished. The HTML pages are in $(BUILDDIR)/html."

dirhtml: sphinx_prereq
	$(SPHINXBUILD) -b dirhtml $(ALLSPHINXOPTS) $(BUILDDIR)/dirhtml
	@echo
	@echo "Build finished. The HTML pages are in $(BUILDDIR)/dirhtml."

pickle: sphinx_prereq
	$(SPHINXBUILD) -b pickle $(ALLSPHINXOPTS) $(BUILDDIR)/pickle
	@echo
	@echo "Build finished; now you can process the pickle files."

json: sphinx_prereq
	$(SPHINXBUILD) -b json $(ALLSPHINXOPTS) $(BUILDDIR)/json
	@echo
	@echo "Build finished; now you can process the JSON files."

htmlhelp: sphinx_prereq
	$(SPHINXBUILD) -b htmlhelp $(ALLSPHINXOPTS) $(BUILDDIR)/htmlhelp
	@echo
	@echo "Build finished; now you can run HTML Help Workshop with the" \
	      ".hhp project file in $(BUILDDIR)/htmlhelp."

qthelp: sphinx_prereq
	$(SPHINXBUILD) -b qthelp $(ALLSPHINXOPTS) $(BUILDDIR)/qthelp
	@echo
	@echo "Build finished; now you can run "qcollectiongenerator" with the" \
	      ".qhcp project file in $(BUILDDIR)/qthelp, like this:"
	@echo "# qcollectiongenerator $(BUILDDIR)/qthelp/Varnish.qhcp"
	@echo "To view the help file:"
	@echo "# assistant -collectionFile $(BUILDDIR)/qthelp/Varnish.qhc"

latex: sphinx_prereq
	$(SPHINXBUILD) -b latex $(ALLSPHINXOPTS) $(BUILDDIR)/latex
	@echo
	@echo "Build finished; the LaTeX files are in $(BUILDDIR)/latex."
	@echo "Run \`make all-pdf' or \`make all-ps' in that directory to" \
	      "run these through (pdf)latex."

changes: sphinx_prereq
	$(SPHINXBUILD) -b changes $(ALLSPHINXOPTS) $(BUILDDIR)/changes
	@echo
	@echo "The overview file is in $(BUILDDIR)/changes."

linkcheck: sphinx_prereq
	$(SPHINXBUILD) -b linkcheck $(ALLSPHINXOPTS) $(BUILDDIR)/linkcheck
	@echo
	@echo "Link check complete; look for any errors in the above output " \
	      "or in $(BUILDDIR)/linkcheck/output.txt."

doctest: sphinx_prereq
	$(SPHINXBUILD) -b doctest $(ALLSPHINXOPTS) $(BUILDDIR)/doctest
	@echo "Testing of doctests in the sources finished, look at the " \
	      "results in $(BUILDDIR)/doctest/output.txt."

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
BUILT_SOURCES = include/cli.rst

include/params.rst: $(top_builddir)/bin/varnishd/varnishd
	$(top_builddir)/bin/varnishd/varnishd -x parameter > ${@}_
	mv ${@}_ ${@}
BUILT_SOURCES += include/params.rst

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

BUILT_SOURCES += include/counters.rst

include/varnishncsa_options.rst: $(top_builddir)/bin/varnishncsa/varnishncsa
	$(top_builddir)/bin/varnishncsa/varnishncsa --options > ${@}_
	mv ${@}_ ${@}
include/varnishncsa_synopsis.rst: $(top_builddir)/bin/varnishncsa/varnishncsa
	$(top_builddir)/bin/varnishncsa/varnishncsa --synopsis > ${@}_
	mv ${@}_ ${@}
BUILT_SOURCES += include/varnishncsa_options.rst \
	include/varnishncsa_synopsis.rst

include/varnishlog_options.rst: $(top_builddir)/bin/varnishlog/varnishlog
	$(top_builddir)/bin/varnishlog/varnishlog --options > ${@}_
	mv ${@}_ ${@}
include/varnishlog_synopsis.rst: $(top_builddir)/bin/varnishlog/varnishlog
	$(top_builddir)/bin/varnishlog/varnishlog --synopsis > ${@}_
	mv ${@}_ ${@}
BUILT_SOURCES += include/varnishlog_options.rst \
	include/varnishlog_synopsis.rst

include/varnishtop_options.rst: $(top_builddir)/bin/varnishtop/varnishtop
	$(top_builddir)/bin/varnishtop/varnishtop --options > ${@}_
	mv ${@}_ ${@}
include/varnishtop_synopsis.rst: $(top_builddir)/bin/varnishtop/varnishtop
	$(top_builddir)/bin/varnishtop/varnishtop --synopsis > ${@}_
	mv ${@}_ ${@}
BUILT_SOURCES += include/varnishtop_options.rst \
	 include/varnishtop_synopsis.rst

include/varnishhist_options.rst: $(top_builddir)/bin/varnishhist/varnishhist
	$(top_builddir)/bin/varnishhist/varnishhist --options > ${@}_
	mv ${@}_ ${@}
include/varnishhist_synopsis.rst: $(top_builddir)/bin/varnishhist/varnishhist
	$(top_builddir)/bin/varnishhist/varnishhist --synopsis > ${@}_
	mv ${@}_ ${@}
BUILT_SOURCES += include/varnishhist_options.rst \
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
BUILT_SOURCES += include/varnishstat_options.rst \
	 include/varnishstat_synopsis.rst \
	 include/varnishstat_bindings.rst

include/vsl-tags.rst: $(top_builddir)/lib/libvarnishapi/vsl2rst
	$(top_builddir)/lib/libvarnishapi/vsl2rst > ${@}_
	mv ${@}_ ${@}
BUILT_SOURCES += include/vsl-tags.rst

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
BUILT_SOURCES += include/vtc-syntax.rst

# XXX copy/paste rules need some TLC

include/vmod_std.generated.rst: $(top_builddir)/lib/libvmod_std/vmod_std.rst
	cp $(top_builddir)/lib/libvmod_std/vmod_std.rst $@
BUILT_SOURCES += include/vmod_std.generated.rst

include/vmod_directors.generated.rst: $(top_builddir)/lib/libvmod_directors/vmod_directors.rst
	cp $(top_builddir)/lib/libvmod_directors/vmod_directors.rst $@
BUILT_SOURCES += include/vmod_directors.generated.rst

include/vmod_purge.generated.rst: $(top_builddir)/lib/libvmod_purge/vmod_purge.rst
	cp $(top_builddir)/lib/libvmod_purge/vmod_purge.rst $@
BUILT_SOURCES += include/vmod_purge.generated.rst

include/vmod_vtc.generated.rst: $(top_builddir)/lib/libvmod_vtc/vmod_vtc.rst
	cp $(top_builddir)/lib/libvmod_vtc/vmod_vtc.rst $@
BUILT_SOURCES += include/vmod_vtc.generated.rst

include/vmod_blob.generated.rst: $(top_builddir)/lib/libvmod_blob/vmod_blob.rst
	cp $(top_builddir)/lib/libvmod_blob/vmod_blob.rst $@
BUILT_SOURCES += include/vmod_blob.generated.rst

include/vmod_cookie.generated.rst: $(top_builddir)/lib/libvmod_cookie/vmod_cookie.rst
	cp $(top_builddir)/lib/libvmod_cookie/vmod_cookie.rst $@
BUILT_SOURCES += include/vmod_cookie.generated.rst

include/vmod_unix.generated.rst: $(top_builddir)/lib/libvmod_unix/vmod_unix.rst
	cp $(top_builddir)/lib/libvmod_unix/vmod_unix.rst $@
BUILT_SOURCES += include/vmod_unix.generated.rst

include/vmod_proxy.generated.rst: $(top_builddir)/lib/libvmod_proxy/vmod_proxy.rst
	cp $(top_builddir)/lib/libvmod_proxy/vmod_proxy.rst $@
BUILT_SOURCES += include/vmod_proxy.generated.rst

EXTRA_DIST += $(BUILT_SOURCES)
CLEANFILES = $(BUILT_SOURCES)

.NOPATH: $(BUILT_SOURCES)
