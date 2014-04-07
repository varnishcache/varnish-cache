# Makefile for Sphinx documentation
#

# You can set these variables from the command line.
SPHINXOPTS    =
SPHINXBUILD   = sphinx-build
PAPER         = a4
BUILDDIR      = build

# Internal variables.
PAPEROPT_a4     = -D latex_paper_size=a4
PAPEROPT_letter = -D latex_paper_size=letter
ALLSPHINXOPTS   = -d $(BUILDDIR)/doctrees $(PAPEROPT_$(PAPER)) $(SPHINXOPTS) $(srcdir)

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
	-rm -rf $(BUILDDIR)/*

html:
	$(SPHINXBUILD) -b html $(ALLSPHINXOPTS) $(BUILDDIR)/html
	@echo
	@echo "Build finished. The HTML pages are in $(BUILDDIR)/html."

dirhtml:
	$(SPHINXBUILD) -b dirhtml $(ALLSPHINXOPTS) $(BUILDDIR)/dirhtml
	@echo
	@echo "Build finished. The HTML pages are in $(BUILDDIR)/dirhtml."

pickle:
	$(SPHINXBUILD) -b pickle $(ALLSPHINXOPTS) $(BUILDDIR)/pickle
	@echo
	@echo "Build finished; now you can process the pickle files."

json:
	$(SPHINXBUILD) -b json $(ALLSPHINXOPTS) $(BUILDDIR)/json
	@echo
	@echo "Build finished; now you can process the JSON files."

htmlhelp:
	$(SPHINXBUILD) -b htmlhelp $(ALLSPHINXOPTS) $(BUILDDIR)/htmlhelp
	@echo
	@echo "Build finished; now you can run HTML Help Workshop with the" \
	      ".hhp project file in $(BUILDDIR)/htmlhelp."

qthelp:
	$(SPHINXBUILD) -b qthelp $(ALLSPHINXOPTS) $(BUILDDIR)/qthelp
	@echo
	@echo "Build finished; now you can run "qcollectiongenerator" with the" \
	      ".qhcp project file in $(BUILDDIR)/qthelp, like this:"
	@echo "# qcollectiongenerator $(BUILDDIR)/qthelp/Varnish.qhcp"
	@echo "To view the help file:"
	@echo "# assistant -collectionFile $(BUILDDIR)/qthelp/Varnish.qhc"

latex:
	$(SPHINXBUILD) -b latex $(ALLSPHINXOPTS) $(BUILDDIR)/latex
	@echo
	@echo "Build finished; the LaTeX files are in $(BUILDDIR)/latex."
	@echo "Run \`make all-pdf' or \`make all-ps' in that directory to" \
	      "run these through (pdf)latex."

changes:
	$(SPHINXBUILD) -b changes $(ALLSPHINXOPTS) $(BUILDDIR)/changes
	@echo
	@echo "The overview file is in $(BUILDDIR)/changes."

linkcheck:
	$(SPHINXBUILD) -b linkcheck $(ALLSPHINXOPTS) $(BUILDDIR)/linkcheck
	@echo
	@echo "Link check complete; look for any errors in the above output " \
	      "or in $(BUILDDIR)/linkcheck/output.txt."

doctest:
	$(SPHINXBUILD) -b doctest $(ALLSPHINXOPTS) $(BUILDDIR)/doctest
	@echo "Testing of doctests in the sources finished, look at the " \
	      "results in $(BUILDDIR)/doctest/output.txt."

EXTRA_DIST = \
	conf.py \
	index.rst \
	glossary/index.rst \
	installation/bugs.rst \
	installation/help.rst \
	installation/index.rst \
	installation/install.rst \
	installation/platformnotes.rst \
	installation/prerequisites.rst \
	phk/autocrap.rst \
	phk/backends.rst \
	phk/barriers.rst \
	phk/gzip.rst \
	phk/http20.rst \
	phk/index.rst \
	phk/ipv6suckage.rst \
	phk/platforms.rst \
	phk/spdy.rst \
	phk/sphinx.rst \
	phk/ssl.rst \
	phk/thetoolsweworkwith.rst \
	phk/thoughts.rst \
	phk/three-zero.rst \
	phk/varnish_does_not_hash.rst \
	phk/vcl_expr.rst \
	phk/wanton_destruction.rst \
	reference/index.rst \
	reference/varnish-cli.rst \
	reference/varnishadm.rst \
	reference/varnishd.rst \
	reference/varnishhist.rst \
	reference/varnishlog.rst \
	reference/varnishncsa.rst \
	reference/varnishstat.rst \
	reference/varnishtest.rst \
	reference/varnishtop.rst \
	reference/vcl.rst \
	reference/vmod.rst \
	reference/vsl-query.rst \
	reference/vsl.rst \
	reference/vsm.rst \
	tutorial/backend_servers.rst \
	tutorial/index.rst \
	tutorial/introduction.rst \
	tutorial/now_what.rst \
	tutorial/peculiarities.rst \
	tutorial/putting_varnish_on_port_80.rst \
	tutorial/starting_varnish.rst \
	users-guide/command-line.rst \
	users-guide/compression.rst \
	users-guide/devicedetection.rst \
	users-guide/esi.rst \
	users-guide/increasing-your-hitrate.rst \
	users-guide/index.rst \
	users-guide/intro.rst \
	users-guide/operation-logging.rst \
	users-guide/operation-statistics.rst \
	users-guide/params.rst \
	users-guide/performance.rst \
	users-guide/purging.rst \
	users-guide/report.rst \
	users-guide/run_cli.rst \
	users-guide/run_security.rst \
	users-guide/running.rst \
	users-guide/sizing-your-cache.rst \
	users-guide/storage-backends.rst \
	users-guide/troubleshooting.rst \
	users-guide/vcl-actions.rst \
	users-guide/vcl-backends.rst \
	users-guide/vcl-built-in-subs.rst \
	users-guide/vcl-example-acls.rst \
	users-guide/vcl-example-manipulating-headers.rst \
	users-guide/vcl-example-manipulating-responses.rst \
	users-guide/vcl-example-websockets.rst \
	users-guide/vcl-examples.rst \
	users-guide/vcl-hashing.rst \
	users-guide/vcl-inline-c.rst \
	users-guide/vcl-intro.rst \
	users-guide/vcl-grace.rst \
	users-guide/vcl-syntax.rst \
	users-guide/vcl-variables.rst \
	users-guide/vcl.rst

dist-hook:
	$(MAKE) html
	cp -r $(BUILDDIR) $(distdir)

distclean-local:
	rm -rf $(BUILDDIR)

include/params.rst: $(top_builddir)/bin/varnishd/varnishd
	$(top_builddir)/bin/varnishd/varnishd -x dumprstparam > include/params.rst
BUILT_SOURCES = include/params.rst

# XXX add varnishstat here when it's been _opt2rst'ed

include/varnishncsa_options.rst: $(top_builddir)/bin/varnishncsa/varnishncsa_opt2rst
	$(top_builddir)/bin/varnishncsa/varnishncsa_opt2rst options > $@
include/varnishncsa_synopsis.rst: $(top_builddir)/bin/varnishncsa/varnishncsa_opt2rst
	$(top_builddir)/bin/varnishncsa/varnishncsa_opt2rst synopsis > $@
BUILT_SOURCES += include/varnishncsa_options.rst \
	include/varnishncsa_synopsis.rst

include/varnishlog_options.rst: $(top_builddir)/bin/varnishlog/varnishlog_opt2rst
	$(top_builddir)/bin/varnishlog/varnishlog_opt2rst options > $@
include/varnishlog_synopsis.rst: $(top_builddir)/bin/varnishlog/varnishlog_opt2rst
	$(top_builddir)/bin/varnishlog/varnishlog_opt2rst synopsis > $@
BUILT_SOURCES += include/varnishlog_options.rst \
	include/varnishlog_synopsis.rst

include/varnishtop_options.rst: $(top_builddir)/bin/varnishtop/varnishtop_opt2rst
	$(top_builddir)/bin/varnishtop/varnishtop_opt2rst options > $@
include/varnishtop_synopsis.rst: $(top_builddir)/bin/varnishtop/varnishtop_opt2rst
	$(top_builddir)/bin/varnishtop/varnishtop_opt2rst synopsis > $@
BUILT_SOURCES += include/varnishtop_options.rst \
	 include/varnishtop_synopsis.rst

include/varnishhist_options.rst: $(top_builddir)/bin/varnishhist/varnishhist_opt2rst
	$(top_builddir)/bin/varnishhist/varnishhist_opt2rst options > $@
include/varnishhist_synopsis.rst: $(top_builddir)/bin/varnishhist/varnishhist_opt2rst
	$(top_builddir)/bin/varnishhist/varnishhist_opt2rst synopsis > $@
BUILT_SOURCES += include/varnishhist_options.rst \
	 include/varnishhist_synopsis.rst

include/vmod_std.rst: $(top_builddir)/lib/libvmod_std/vmod_std.rst
	cp $? $@
BUILT_SOURCES += include/vmod_std.rst

include/vmod_directors.rst: $(top_builddir)/lib/libvmod_directors/vmod_directors.rst
	cp $? $@
BUILT_SOURCES += include/vmod_directors.rst

EXTRA_DIST += $(BUILT_SOURCES)
MAINTAINERCLEANFILES = $(EXTRA_DIST)
