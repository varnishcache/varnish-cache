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
	common/common_vsm.c \
	common/common_vsc.c \
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

noinst_HEADERS = \
	builtin_vcl.h \
	cache/cache_ban.h \
	cache/cache_esi.h \
	cache/cache_obj.h \
	cache/cache_pool.h \
	cache/cache_priv.h \
	cache/cache_transport.h \
	common/heritage.h \
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

varnishd_CFLAGS = \
	@PCRE_CFLAGS@ \
	@SAN_CFLAGS@ \
	-DVARNISHD_IS_NOT_A_VMOD \
	-DVARNISH_STATE_DIR='"${VARNISH_STATE_DIR}"' \
	-DVARNISH_VMOD_DIR='"${pkglibdir}/vmods"' \
	-DVARNISH_VCL_DIR='"${varnishconfdir}:${datarootdir}/varnish/vcl"'

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
vhp_gen_hufdec_CFLAGS = -include config.h
vhp_gen_hufdec_LDADD = \
	$(top_builddir)/lib/libvarnish/libvarnish.a

noinst_PROGRAMS += vhp_table_test
vhp_table_test_SOURCES = hpack/vhp_table.c
vhp_table_test_CFLAGS = -DTABLE_TEST_DRIVER -include config.h
vhp_table_test_LDADD = \
	$(top_builddir)/lib/libvarnish/libvarnish.a

noinst_PROGRAMS += vhp_decode_test
vhp_decode_test_SOURCES = hpack/vhp_decode.c hpack/vhp_table.c
vhp_decode_test_CFLAGS = -DDECODE_TEST_DRIVER -include config.h
vhp_decode_test_LDADD = \
	$(top_builddir)/lib/libvarnish/libvarnish.a

TESTS = vhp_table_test vhp_decode_test

#
# Turn the builtin.vcl file into a C-string we can include in the program.
#
builtin_vcl.h:	builtin.vcl
	echo '/*' > $@
	echo ' * NB:  This file is machine generated, DO NOT EDIT!' >> $@
	echo ' *' >> $@
	echo ' * Edit builtin.vcl instead and run make' >> $@
	echo ' *' >> $@
	echo ' */' >> $@
	echo '' >> $@
	sed -e 's/"/\\"/g' \
	    -e 's/$$/\\n"/' \
	    -e 's/^/ "/' $(srcdir)/builtin.vcl >> $@

EXTRA_DIST = builtin.vcl

vhp_hufdec.h: vhp_gen_hufdec
	$(AM_V_GEN) ./vhp_gen_hufdec > vhp_hufdec.h_
	mv vhp_hufdec.h_ vhp_hufdec.h

DISTCLEANFILES = builtin_vcl.h

BUILT_SOURCES	=	vhp_hufdec.h
DISTCLEANFILES	+=	vhp_hufdec.h

# Explicitly record dependency
mgt/mgt_vcc.c:	builtin_vcl.h
