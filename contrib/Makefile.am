#

if !WITH_CONTRIB
dist_noinst_SCRIPTS = \
	varnishstatdiff
else
dist_bin_SCRIPTS = \
	varnishstatdiff

TESTS = @CONTRIB_TESTS@

include $(top_srcdir)/vtc.am
endif

EXTRA_DIST = @CONTRIB_TESTS@
