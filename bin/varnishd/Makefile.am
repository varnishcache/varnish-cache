#

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libvgz \
	-I$(top_builddir)/bin/varnishd \
	-I$(top_builddir)/include

sbin_PROGRAMS = varnishd

varnishd_SOURCES = \
	cache/cache_acceptor.c \
	cache/cache_backend.c \
	cache/cache_backend_cfg.c \
	cache/cache_backend_probe.c \
	cache/cache_backend_tcp.c \
	cache/cache_ban.c \
	cache/cache_ban_build.c \
	cache/cache_ban_lurker.c \
	cache/cache_busyobj.c \
	cache/cache_cli.c \
	cache/cache_deliver_proc.c \
	cache/cache_director.c \
	cache/cache_esi_deliver.c \
	cache/cache_esi_fetch.c \
	cache/cache_esi_parse.c \
	cache/cache_expire.c \
	cache/cache_fetch.c \
	cache/cache_fetch_proc.c \
	cache/cache_gzip.c \
	cache/cache_hash.c \
	cache/cache_http.c \
	cache/cache_lck.c \
	cache/cache_main.c \
	cache/cache_mempool.c \
	cache/cache_obj.c \
	cache/cache_panic.c \
	cache/cache_pool.c \
	cache/cache_req.c \
	cache/cache_req_body.c \
	cache/cache_req_fsm.c \
	cache/cache_rfc2616.c \
	cache/cache_range.c \
	cache/cache_session.c \
	cache/cache_shmlog.c \
	cache/cache_vary.c \
	cache/cache_vcl.c \
	cache/cache_vrt.c \
	cache/cache_vrt_priv.c \
	cache/cache_vrt_re.c \
	cache/cache_vrt_var.c \
	cache/cache_vrt_vmod.c \
	cache/cache_wrk.c \
	cache/cache_ws.c \
	common/common_vsc.c \
	common/common_vsm.c \
	hash/hash_classic.c \
	hash/hash_critbit.c \
	hash/mgt_hash.c \
	hash/hash_simple_list.c \
	hpack/vhp_table.c \
	hpack/vhp_decode.c \
	http1/cache_http1_deliver.c \
	http1/cache_http1_fetch.c \
	http1/cache_http1_fsm.c \
	http1/cache_http1_line.c \
	http1/cache_http1_pipe.c \
	http1/cache_http1_proto.c \
	http1/cache_http1_vfp.c \
	http2/cache_http2_deliver.c \
	http2/cache_http2_hpack.c \
	http2/cache_http2_panic.c \
	http2/cache_http2_proto.c \
	http2/cache_http2_session.c \
	http2/cache_http2_send.c \
	mgt/mgt_acceptor.c \
	mgt/mgt_child.c \
	mgt/mgt_cli.c \
	mgt/mgt_jail.c \
	mgt/mgt_jail_unix.c \
	mgt/mgt_jail_solaris.c \
	mgt/mgt_main.c \
	mgt/mgt_param.c \
	mgt/mgt_param_tbl.c \
	mgt/mgt_param_bits.c \
	mgt/mgt_param_tcp.c \
	mgt/mgt_param_tweak.c \
	mgt/mgt_pool.c \
	mgt/mgt_shmem.c \
	mgt/mgt_util.c \
	mgt/mgt_vcc.c \
	mgt/mgt_vcl.c \
	proxy/cache_proxy_proto.c \
	storage/stevedore.c \
	storage/mgt_stevedore.c \
	storage/stevedore_utils.c \
	storage/storage_file.c \
	storage/storage_lru.c \
	storage/storage_malloc.c \
	storage/storage_persistent.c \
	storage/mgt_storage_persistent.c \
	storage/storage_persistent_silo.c \
	storage/storage_persistent_subr.c \
	storage/storage_simple.c \
	waiter/mgt_waiter.c \
	waiter/cache_waiter.c \
	waiter/cache_waiter_epoll.c \
	waiter/cache_waiter_kqueue.c \
	waiter/cache_waiter_poll.c \
	waiter/cache_waiter_ports.c

nodist_varnishd_SOURCES = \
	builtin_vcl.c

noinst_HEADERS = \
	VSC_main.h \
	cache/cache_ban.h \
	cache/cache_esi.h \
	cache/cache_obj.h \
	cache/cache_pool.h \
	cache/cache_priv.h \
	cache/cache_transport.h \
	common/heritage.h \
	common/common_vsm.h \
	hash/hash_slinger.h \
	hpack/vhp.h \
	http1/cache_http1.h \
	http2/cache_http2.h \
	mgt/mgt.h \
	mgt/mgt_param.h \
	storage/storage.h \
	storage/storage_simple.h \
	storage/storage_persistent.h \
	waiter/waiter_priv.h \
	waiter/mgt_waiter.h

# Headers for use with vmods
nobase_pkginclude_HEADERS = \
	cache/cache.h \
	cache/cache_filter.h \
	cache/cache_backend.h \
	cache/cache_director.h \
	common/common.h \
	common/com_params.h \
	waiter/waiter.h

pkgdatadir = ${datarootdir}/${PACKAGE}/vcl

varnishd_CFLAGS = \
	@PCRE_CFLAGS@ \
	@SAN_CFLAGS@ \
	-DVARNISHD_IS_NOT_A_VMOD \
	-DVARNISH_STATE_DIR='"${VARNISH_STATE_DIR}"' \
	-DVARNISH_VMOD_DIR='"${pkglibdir}/vmods"' \
	-DVARNISH_VCL_DIR='"${pkgsysconfdir}:${pkgdatadir}"'

varnishd_LDFLAGS = -export-dynamic

varnishd_LDADD = \
	$(top_builddir)/lib/libvarnish/libvarnish.a \
	$(top_builddir)/lib/libvcc/libvcc.a \
	$(top_builddir)/lib/libvgz/libvgz.a \
	@SAN_LDFLAGS@ \
	@JEMALLOC_LDADD@ \
	@PCRE_LIBS@ \
	${DL_LIBS} ${PTHREAD_LIBS} ${NET_LIBS} ${RT_LIBS} ${LIBM}

noinst_PROGRAMS = vhp_gen_hufdec
vhp_gen_hufdec_SOURCES = hpack/vhp_gen_hufdec.c
vhp_gen_hufdec_CFLAGS = @SAN_CFLAGS@ \
			-include config.h
vhp_gen_hufdec_LDADD = \
	$(top_builddir)/lib/libvarnish/libvarnish.a

noinst_PROGRAMS += vhp_table_test
vhp_table_test_SOURCES = hpack/vhp_table.c
vhp_table_test_CFLAGS = @SAN_CFLAGS@ \
			-DTABLE_TEST_DRIVER -include config.h
vhp_table_test_LDADD = \
	$(top_builddir)/lib/libvarnish/libvarnish.a

noinst_PROGRAMS += vhp_decode_test
vhp_decode_test_SOURCES = hpack/vhp_decode.c hpack/vhp_table.c
vhp_decode_test_CFLAGS = @SAN_CFLAGS@ \
			 -DDECODE_TEST_DRIVER -include config.h
vhp_decode_test_LDADD = \
	$(top_builddir)/lib/libvarnish/libvarnish.a

TESTS = vhp_table_test vhp_decode_test

#
# Turn the builtin.vcl file into a C-string we can include in the program.
#
builtin_vcl.c:	builtin.vcl
	echo '/*' > $@
	echo ' * NB:  This file is machine generated, DO NOT EDIT!' >> $@
	echo ' *' >> $@
	echo ' * Edit builtin.vcl instead and run make' >> $@
	echo ' *' >> $@
	echo ' */' >> $@
	echo '#include "mgt/mgt.h"' >> $@
	echo '' >> $@
	echo 'const char * const builtin_vcl =' >> $@
	sed -e 's/"/\\"/g' \
	    -e 's/$$/\\n"/' \
	    -e 's/^/ "/' $(srcdir)/builtin.vcl >> $@
	echo ';' >> $@

EXTRA_DIST = builtin.vcl

vhp_hufdec.h: vhp_gen_hufdec
	$(AM_V_GEN) ./vhp_gen_hufdec > vhp_hufdec.h_
	mv vhp_hufdec.h_ vhp_hufdec.h

DISTCLEANFILES = builtin_vcl.c

BUILT_SOURCES	=	vhp_hufdec.h
DISTCLEANFILES	+=	vhp_hufdec.h

#######################################################################
VSC_main.c VSC_main.h: $(srcdir)/main.vsc $(top_builddir)/lib/libvcc/vsctool.py
	$(PYTHON) $(top_srcdir)/lib/libvcc/vsctool.py -ch $(srcdir)/main.vsc

$(varnishd_OBJECTS):	VSC_main.h

EXTRA_DIST	+=	main.vsc
DISTCLEANFILES	+=	VSC_main.c VSC_main.h
nodist_varnishd_SOURCES +=	VSC_main.c

#######################################################################

VSC_lck.c VSC_lck.h: $(srcdir)/lck.vsc $(top_builddir)/lib/libvcc/vsctool.py
	$(PYTHON) $(top_srcdir)/lib/libvcc/vsctool.py -ch $(srcdir)/lck.vsc

$(varnishd_OBJECTS):	VSC_lck.h

EXTRA_DIST	+=	lck.vsc
DISTCLEANFILES	+=	VSC_lck.c VSC_lck.h
nodist_varnishd_SOURCES +=	VSC_lck.c

#######################################################################

VSC_vbe.c VSC_vbe.h: $(srcdir)/vbe.vsc $(top_builddir)/lib/libvcc/vsctool.py
	$(PYTHON) $(top_srcdir)/lib/libvcc/vsctool.py -ch $(srcdir)/vbe.vsc

$(varnishd_OBJECTS):	VSC_vbe.h

EXTRA_DIST	+=	vbe.vsc
DISTCLEANFILES	+=	VSC_vbe.c VSC_vbe.h
nodist_varnishd_SOURCES +=	VSC_vbe.c

#######################################################################

VSC_sma.c VSC_sma.h: $(srcdir)/sma.vsc $(top_builddir)/lib/libvcc/vsctool.py
	$(PYTHON) $(top_srcdir)/lib/libvcc/vsctool.py -ch $(srcdir)/sma.vsc

$(varnishd_OBJECTS):	VSC_sma.h

EXTRA_DIST	+=	sma.vsc
DISTCLEANFILES	+=	VSC_sma.c VSC_sma.h
nodist_varnishd_SOURCES +=	VSC_sma.c

#######################################################################

VSC_smf.c VSC_smf.h: $(srcdir)/smf.vsc $(top_builddir)/lib/libvcc/vsctool.py
	$(PYTHON) $(top_srcdir)/lib/libvcc/vsctool.py -ch $(srcdir)/smf.vsc

$(varnishd_OBJECTS):	VSC_smf.h

EXTRA_DIST	+=	smf.vsc
DISTCLEANFILES	+=	VSC_smf.c VSC_smf.h
nodist_varnishd_SOURCES +=	VSC_smf.c

#######################################################################

VSC_mempool.c VSC_mempool.h: $(srcdir)/mempool.vsc $(top_builddir)/lib/libvcc/vsctool.py
	$(PYTHON) $(top_srcdir)/lib/libvcc/vsctool.py -ch $(srcdir)/mempool.vsc

$(varnishd_OBJECTS):	VSC_mempool.h

EXTRA_DIST	+=	mempool.vsc
DISTCLEANFILES	+=	VSC_mempool.c VSC_mempool.h
nodist_varnishd_SOURCES +=	VSC_mempool.c

#######################################################################

VSC_mgt.c VSC_mgt.h: $(srcdir)/mgt.vsc $(top_builddir)/lib/libvcc/vsctool.py
	$(PYTHON) $(top_srcdir)/lib/libvcc/vsctool.py -ch $(srcdir)/mgt.vsc

$(varnishd_OBJECTS):	VSC_mgt.h

EXTRA_DIST	+=	mgt.vsc
DISTCLEANFILES	+=	VSC_mgt.c VSC_mgt.h
nodist_varnishd_SOURCES +=	VSC_mgt.c

