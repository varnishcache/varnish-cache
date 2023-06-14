# Makefile for graphviz outputs

SUFFIXES: .dot .pdf .svg

# for an out-of-tree build, sphinx needs the output in builddir
# XXX is there a better way to do this?

.PHONY: link_srcdir

link_srcdir:
	if test "x$(srcdir)" != "x$(builddir)" && \
	   test ! -f $(builddir)/cache_http1_fsm.svg ; then \
		d=`pwd`/$(builddir) ; \
		cd $(srcdir) && find . -name \*.svg -type f | \
		cpio -dmp $${d} || true ; \
	fi

dist-hook:
	$(MAKE) html

# You can set these variables from the command line.
# this is a4, letter is 8.5,11
SIZE	      = 8.4,11.7

EXTRA_DIST = $(srcdir)/*.dot	\
	$(SVGS)

PDFS = \
	cache_http1_fsm.pdf \
	cache_req_fsm.pdf \
	cache_fetch.pdf

SVGS = \
	cache_http1_fsm.svg \
	cache_req_fsm.svg \
	cache_fetch.svg

CLEANFILES = \
	$(PDFS)

pdf: $(PDFS)

html: $(SVGS) link_srcdir

# XXX does not fit onto a4 unless in landscape
cache_fetch.pdf: cache_fetch.dot
	$(AM_V_GEN) $(DOT) -Tpdf -Gsize=$(SIZE) -Grotate=90 $< >$@

.dot.pdf:
	$(AM_V_GEN) $(DOT) -Tpdf -Gsize=$(SIZE) $< >$@

.dot.svg:
	$(AM_V_GEN) $(DOT) -Tsvg $< >$@
