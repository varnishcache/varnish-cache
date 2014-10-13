# Makefile for graphviz outputs
#

# You can set these variables from the command line.
# this is a4, letter is 8.5,11
SIZE	      = 8.4,11.7

if HAVE_DOT

PDFS = \
	cache_http1_fsm.pdf \
	cache_req_fsm.pdf \
	cache_fetch.pdf

SVGS = \
	cache_http1_fsm.svg \
	cache_req_fsm.svg \
	cache_fetch.svg

pdf: $(PDFS)

html: $(SVGS)

endif

# XXX does not fit onto a4 unless in landscape
cache_fetch.pdf: cache_fetch.dot
	@DOT@ -Tpdf -Gsize=$(SIZE) -Grotate=90 $< >$@

%.pdf: %.dot
	@DOT@ -Tpdf -Gsize=$(SIZE) $< >$@

%.svg: %.dot
	@DOT@ -Tsvg $< >$@
