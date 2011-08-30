#

pkginclude_HEADERS = \
	vsm.h \
	vsl.h \
	vcli.h \
	vsl_tags.h \
	vsc_fields.h \
	vsc_all.h \
	vsc.h \
	varnishapi.h

nobase_noinst_HEADERS = \
	ban_vars.h \
	binary_heap.h \
	cli_common.h \
	cli_priv.h \
	cli_serve.h \
	compat/daemon.h \
	compat/execinfo.h \
	compat/srandomdev.h \
	flopen.h \
	http_headers.h \
	http_response.h \
	libvarnish.h \
	libvcl.h \
	miniobj.h \
	persistent.h \
	vas.h \
	vav.h \
	vsha256.h \
	vqueue.h \
	vpf.h \
	vsb.h \
	vcl.h \
	vcl_returns.h \
	vcs_version.h \
	vct.h \
	vend.h \
	vev.h \
	vin.h \
	vlu.h \
	vbm.h \
	vmb.h \
	vmod_abi.h \
	vre.h \
	vrt.h \
	vrt_obj.h \
	vrt_stv_var.h \
	vss.h

vrt_stv_var.h vcl_returns.h vcl.h vrt_obj.h: $(top_srcdir)/lib/libvcl/generate.py $(top_srcdir)/include/vrt.h
	@PYTHON@ $(top_srcdir)/lib/libvcl/generate.py $(top_srcdir) $(top_builddir)

BUILT_SOURCES = vcs_version.h vmod_abi.h
MAINTAINERCLEANFILES = vcs_version.h 
vcs_version.h: FORCE
	@if [ -d "$(top_srcdir)/.git" ]; then \
		V="$$(git show -s --pretty=format:%h)" \
		H="$$(head -n 1 vcs_version.h 2>/dev/null || true)"; \
		if [ "/* $$V */" != "$$H" ]; then \
		( \
		  echo "/* $$V */" ;\
		  echo '/*' ;\
		  echo ' * NB:  This file is machine generated, DO NOT EDIT!' ;\
		  echo ' *' ;\
		  echo ' * Run make to regenerate' ;\
		  echo ' *'  ;\
		  echo ' */' ;\
		  echo "/* $$V */" ;\
		  echo '' ;\
		  echo "#define VCS_Version \"$$V\"" \
		) > vcs_version.h ; \
		fi \
	else \
		if [ ! -f vcs_version.h ]; then \
		( \
		  echo "/* NOGIT */" ; \
		  echo '/* No git commit ID available, see include/Makefile.am for explanation */' ; \
		  echo '#define VCS_Version "NOGIT"' \
		) >  vcs_version.h ; \
		fi \
	fi
FORCE:

# If vcs_version contains NOGIT, Varnish has not been built from a
# tarball made with make dist, nor from a git checkout, so there's no
# way for us to give strong guarantees about what version you're
# actually running.
#
# The way to fix this is to either build Varnish from a tarball made
# with `make dist` or a git checkout.

vmod_abi.h: vcs_version.h
	@GITID=$$(sed 's/[^0-9a-f]//g;q' vcs_version.h) ; \
	if [ -z "$$GITID" ]; then \
		echo "warning: weak VMOD ABI checking, see include/Makefile.am" ; \
	fi ; \
	echo "#define VMOD_ABI_Version \"@PACKAGE_STRING@ $$GITID\"" > vmod_abi.h

CLEANFILES = vcl_returns.h \
	vcl.h \
	vrt_obj.h \
	vmod_abi.h
