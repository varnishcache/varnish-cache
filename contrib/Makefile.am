#

include $(top_srcdir)/vtc.am

if !WITH_CONTRIB
dist_noinst_SCRIPTS = \
	varnishstatdiff
else
dist_bin_SCRIPTS = \
	varnishstatdiff

TESTS = @CONTRIB_TESTS@

endif

EXTRA_DIST = @CONTRIB_TESTS@
