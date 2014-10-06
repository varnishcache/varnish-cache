# Makefile for graphviz outputs
#

# You can set these variables from the command line.
# this is a4, letter is 8.5,11
SIZE	      = 8.4,11.7

if HAVE_DOT
if HAVE_PS2PDF

PDFS = \
	cache_http1_fsm.pdf \
	cache_req_fsm.pdf \
	cache_fetch.pdf

pdf: $(PDFS)

endif
endif

%.ps: %.dot
	@DOT@ -Tps -Gsize=$(SIZE) $< >$@

%.pdf: %.ps
	@PS2PDF@ $<
