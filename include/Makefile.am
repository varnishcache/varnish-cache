# $Id$

pkginclude_HEADERS = \
	vsm.h \
	vsl.h \
	vsl_tags.h \
	vsc_fields.h \
	vsc.h \
	varnishapi.h

nobase_noinst_HEADERS = \
	argv.h \
	binary_heap.h \
	cli.h \
	cli_common.h \
	cli_priv.h \
	cli_serve.h \
	compat/daemon.h \
	compat/execinfo.h \
	compat/setproctitle.h \
	compat/srandomdev.h \
	compat/strlcat.h \
	compat/strlcpy.h \
	compat/strndup.h \
	flopen.h \
	http_headers.h \
	http_response.h \
	libvarnish.h \
	libvcl.h \
	miniobj.h \
	persistent.h \
	purge_vars.h \
	svnid.h \
	vas.h \
	vsha256.h \
	vqueue.h \
	vpf.h \
	vsb.h \
	vcl.h \
	vcl_returns.h \
	vct.h \
	vend.h \
	vev.h \
	vin.h \
	vlu.h \
	vbm.h \
	vmb.h \
	vre.h \
	vrt.h \
	vrt_obj.h \
	vss.h

vcl_returns.h vcl.h vrt_obj.h: $(top_srcdir)/lib/libvcl/generate.py $(top_srcdir)/include/vrt.h
	@PYTHON@ $(top_srcdir)/lib/libvcl/generate.py $(top_srcdir) $(top_builddir)

CLEANFILES = vcl_returns.h \
	vcl.h \
	vrt_obj.h
