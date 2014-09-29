# Makefile for Sphinx documentation
#

# You can set these variables from the command line.
SPHINXOPTS    =
SPHINXBUILD   = sphinx-build -W -N
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
	include \
	glossary \
	installation \
	phk \
	tutorial \
	users-guide \
	whats-new

dist-hook:
	$(MAKE) html
	rm -rf $(BUILDDIR)/doctrees
	cp -r $(BUILDDIR) $(distdir)
# Remove any extra non-rst files we may have copied over.
	find $(distdir) -mindepth 2 -type f ! -name \*.rst | grep -v '/build/' | xargs rm -f

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

.PHONY: reference
reference:
	test -d $@ || mkdir $@
BUILT_SOURCES += reference

reference/vmod_std.generated.rst: reference $(top_builddir)/lib/libvmod_std/vmod_std.rst
	cp $(top_builddir)/lib/libvmod_std/vmod_std.rst $@
BUILT_SOURCES += reference/vmod_std.generated.rst

reference/vmod_directors.generated.rst: reference $(top_builddir)/lib/libvmod_directors/vmod_directors.rst
	cp $(top_builddir)/lib/libvmod_directors/vmod_directors.rst $@
BUILT_SOURCES += reference/vmod_directors.generated.rst

EXTRA_DIST += $(BUILT_SOURCES)
MAINTAINERCLEANFILES = $(EXTRA_DIST)
