#

# API headers
nobase_pkginclude_HEADERS = \
	tbl/acct_fields_bereq.h \
	tbl/acct_fields_req.h \
	tbl/backend_poll.h \
	tbl/ban_vars.h \
	tbl/bo_flags.h \
	tbl/boc_state.h \
	tbl/body_status.h \
	tbl/cli_cmds.h \
	tbl/debug_bits.h \
	tbl/feature_bits.h \
	tbl/h2_error.h \
	tbl/h2_frames.h \
	tbl/h2_settings.h \
	tbl/h2_stream.h \
	tbl/http_headers.h \
	tbl/http_response.h \
	tbl/locks.h \
	tbl/obj_attr.h \
	tbl/oc_exp_flags.h \
	tbl/oc_flags.h \
	tbl/params.h \
	tbl/req_body.h \
	tbl/req_flags.h \
	tbl/sess_attr.h \
	tbl/sess_close.h \
	tbl/steps.h \
	tbl/symbol_kind.h \
	tbl/vcc_types.h \
	tbl/vcl_returns.h \
	tbl/vhd_fsm.h \
	tbl/vhd_fsm_funcs.h \
	tbl/vhd_return.h \
	tbl/vhp_huffman.h \
	tbl/vhp_static.h \
	tbl/vrt_stv_var.h \
	tbl/vsc_levels.h \
	tbl/vsl_tags.h \
	tbl/vsl_tags_http.h \
	tbl/waiters.h \
	vapi/vsm.h \
	vapi/vsc.h \
	vapi/vsc_int.h \
	vapi/vsl.h \
	vapi/vsl_int.h \
	vapi/voptget.h \
	vapi/vapi_options.h \
	vcli.h

# Headers for use with vmods
nobase_pkginclude_HEADERS += \
	miniobj.h \
	vas.h \
	vav.h \
	vbm.h \
	vcl.h \
	vcs.h \
	vmod_abi.h \
	vqueue.h \
	vre.h \
	vdef.h \
	vrt.h \
	vrt_obj.h \
	vsa.h \
	vsb.h \
	vsha256.h \
	vtcp.h \
	vtim.h \
	vrnd.h

# Private headers
nobase_noinst_HEADERS = \
	binary_heap.h \
	compat/daemon.h \
	vfl.h \
	libvcc.h \
	vcli_serve.h \
	vcs_version.h \
	vct.h \
	vcurses.h \
	vend.h \
	vev.h \
	vfil.h \
	vin.h \
	vlu.h \
	vmb.h \
	vnum.h \
	vpf.h \
	vsl_priv.h \
	vsmw.h \
	vsm_priv.h \
	vsub.h \
	vss.h \
	vtcp.h \
	vtree.h \
	vut.h \
	vut_options.h

GENERATED_H = \
	tbl/vrt_stv_var.h \
	tbl/vcl_returns.h \
	tbl/vcc_types.h \
	vrt_obj.h \
	vcl.h \
	vcs_version.h \
	vmod_abi.h

$(GENERATED_H): $(top_srcdir)/lib/libvcc/generate.py $(top_srcdir)/include/vrt.h
	mkdir -p tbl
	@PYTHON@ $(top_srcdir)/lib/libvcc/generate.py $(top_srcdir) $(top_builddir)

BUILT_SOURCES = $(GENERATED_H)
MAINTAINERCLEANFILES = vcs_version.h

CLEANFILES = \
	$(GENERATED_H) \
	vrt_test \
	_vrt_test \
	vrt.c

TESTS = vbm_test vrt_test

noinst_PROGRAMS = vbm_test

vbm_test_SOURCES = vbm_test.c vbm.h

vrt.c: vrt.h
	cp $(srcdir)/vrt.h $@

vrt_test: vrt.c
	echo "exec ${CC} -c -o _vrt_test vrt.c" > $@
	chmod +x $@

test: ${TESTS}
	@for test in ${TESTS} ; do ./$${test} ; done
