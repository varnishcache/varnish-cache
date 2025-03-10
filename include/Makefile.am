#

# API headers
nobase_pkginclude_HEADERS = \
	tbl/acct_fields_bereq.h \
	tbl/acct_fields_req.h \
	tbl/backend_poll.h \
	tbl/ban_arg_oper.h \
	tbl/ban_oper.h \
	tbl/ban_vars.h \
	tbl/bereq_flags.h \
	tbl/beresp_flags.h \
	tbl/boc_state.h \
	tbl/body_status.h \
	tbl/cli_cmds.h \
	tbl/debug_bits.h \
	tbl/experimental_bits.h \
	tbl/feature_bits.h \
	tbl/h2_error.h \
	tbl/h2_frames.h \
	tbl/h2_settings.h \
	tbl/h2_stream.h \
	tbl/htc.h \
	tbl/http_headers.h \
	tbl/http_response.h \
	tbl/locks.h \
	tbl/obj_attr.h \
	tbl/oc_exp_flags.h \
	tbl/oc_flags.h \
	tbl/params.h \
	tbl/req_bereq_flags.h \
	tbl/req_flags.h \
	tbl/sess_attr.h \
	tbl/sess_close.h \
	tbl/symbol_kind.h \
	tbl/vcc_feature_bits.h \
	tbl/vcl_returns.h \
	tbl/vcl_context.h \
	tbl/vcl_states.h \
	tbl/vhd_fsm.h \
	tbl/vhd_fsm_funcs.h \
	tbl/vhd_return.h \
	tbl/vhp_huffman.h \
	tbl/vhp_static.h \
	tbl/vrt_stv_var.h \
	tbl/vsc_levels.h \
	tbl/vsig_list.h \
	tbl/vsl_tags.h \
	tbl/vsl_tags_http.h \
	tbl/waiters.h \
	vapi/vsc.h \
	vapi/vsig.h \
	vapi/vsl.h \
	vapi/vsl_int.h \
	vapi/vsm.h \
	vapi/voptget.h \
	vapi/vapi_options.h \
	vcli.h \
	vut.h \
	vut_options.h

# Headers for use with vmods
nobase_pkginclude_HEADERS += \
	vbh.h \
	miniobj.h \
	vas.h \
	vav.h \
	vbm.h \
	vcl.h \
	vcs.h \
	vmod_abi.h \
	vnum.h \
	vqueue.h \
	vre.h \
	vre_pcre2.h \
	vdef.h \
	vrt.h \
	vrt_obj.h \
	vsa.h \
	vsb.h \
	vsha256.h \
	vsync.h \
	vtcp.h \
	vte.h \
	vtim.h \
	vtree.h \
	vrnd.h

# Private headers
nobase_noinst_HEADERS = \
	compat/daemon.h \
	libvcc.h \
	vbt.h \
	vcc_interface.h \
	vcli_serve.h \
	vcs_version.h \
	vct.h \
	vcurses.h \
	venc.h \
	vend.h \
	vev.h \
	vfil.h \
	vfl.h \
	vin.h \
	vjsn.h \
	vlu.h \
	vmb.h \
	vpf.h \
	vsc_priv.h \
	vsl_priv.h \
	vsm_priv.h \
	vsub.h \
	vss.h \
	vtcp.h \
	vus.h

## keep in sync with lib/libvcc/Makefile.am
vcl.h: \
	    $(top_srcdir)/lib/libvcc/generate.py \
	    $(top_srcdir)/include/vcc_interface.h \
	    $(top_srcdir)/include/vdef.h \
	    $(top_srcdir)/include/vrt.h \
	    $(top_srcdir)/doc/sphinx/reference/vcl_var.rst
	mkdir -p $(top_builddir)/include/tbl
	${PYTHON} $(top_srcdir)/lib/libvcc/generate.py \
	    $(top_srcdir) $(top_builddir)

GEN_H = \
	tbl/vcl_context.h \
	tbl/vcl_returns.h \
	tbl/vrt_stv_var.h \
	vrt_obj.h

$(GEN_H): vcl.h

GENERATED_H = vcl.h $(GEN_H)

## vcs_version.h / vmod_abi.h need to be up-to-date with every build
## except when building from a distribution

vcs_version.h:
	@if test -e $(srcdir)/generate.py && \
	    test -e $(top_srcdir)/.git || \
	   ! test -f $(srcdir)/vmod_abi.h || \
	   ! test -f $(srcdir)/vcs_version.h ; then \
	    ${PYTHON} $(srcdir)/generate.py \
		$(top_srcdir) $(top_builddir) ; \
	fi

vmod_abi.h: vcs_version.h

.PHONY: vcs_version.h

##

BUILT_SOURCES = \
	$(GENERATED_H) \
	vcs_version.h \
	vmod_abi.h \
	vrt_test.c

DISTCLEANFILES = $(GENERATED_H)

CLEANFILES = vrt_test.c

noinst_PROGRAMS = vbm_test vrt_test

vbm_test_SOURCES = vbm_test.c vbm.h

vrt_test.c: vdef.h vrt.h
	$(AM_V_GEN) cat $(srcdir)/vdef.h $(srcdir)/vrt.h > $@

vrt_test_CFLAGS = -c
vrt_test_LINK = echo >$@

TESTS = vbm_test
