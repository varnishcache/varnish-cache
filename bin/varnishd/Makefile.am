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
	cache/cache_backend_poll.c \
	cache/cache_ban.c \
	cache/cache_busyobj.c \
	cache/cache_cli.c \
	cache/cache_dir.c \
	cache/cache_esi_deliver.c \
	cache/cache_esi_fetch.c \
	cache/cache_esi_parse.c \
	cache/cache_expire.c \
	cache/cache_fetch.c \
	cache/cache_fetch_proc.c \
	cache/cache_gzip.c \
	cache/cache_hash.c \
	cache/cache_http.c \
	cache/cache_http1_fetch.c \
	cache/cache_http1_fsm.c \
	cache/cache_http1_proto.c \
	cache/cache_lck.c \
	cache/cache_main.c \
	cache/cache_mempool.c \
	cache/cache_obj.c \
	cache/cache_panic.c \
	cache/cache_pipe.c \
	cache/cache_pool.c \
	cache/cache_req_fsm.c \
	cache/cache_http1_deliver.c \
	cache/cache_rfc2616.c \
	cache/cache_session.c \
	cache/cache_shmlog.c \
	cache/cache_vary.c \
	cache/cache_vcl.c \
	cache/cache_vrt.c \
	cache/cache_vrt_re.c \
	cache/cache_vrt_var.c \
	cache/cache_vrt_vmod.c \
	cache/cache_wrk.c \
	cache/cache_wrw.c \
	cache/cache_ws.c \
	common/common_vsm.c \
	common/common_vsc.c \
	hash/hash_classic.c \
	hash/hash_critbit.c \
	hash/hash_mgt.c \
	hash/hash_simple_list.c \
	mgt/mgt_child.c \
	mgt/mgt_cli.c \
	mgt/mgt_main.c \
	mgt/mgt_param.c \
	mgt/mgt_param_tbl.c \
	mgt/mgt_param_bits.c \
	mgt/mgt_param_tcp.c \
	mgt/mgt_param_tweak.c \
	mgt/mgt_pool.c \
	mgt/mgt_sandbox.c \
	mgt/mgt_sandbox_solaris.c \
	mgt/mgt_shmem.c \
	mgt/mgt_vcc.c \
	storage/stevedore.c \
	storage/stevedore_mgt.c \
	storage/stevedore_utils.c \
	storage/storage_file.c \
	storage/storage_malloc.c \
	storage/storage_persistent.c \
	storage/storage_persistent_mgt.c \
	storage/storage_persistent_silo.c \
	storage/storage_persistent_subr.c \
	storage/storage_umem.c \
	waiter/mgt_waiter.c \
	waiter/cache_waiter.c \
	waiter/cache_waiter_epoll.c \
	waiter/cache_waiter_kqueue.c \
	waiter/cache_waiter_poll.c \
	waiter/cache_waiter_ports.c 

noinst_HEADERS = \
	cache/cache_backend.h \
	cache/cache_esi.h \
	common/heritage.h \
	builtin_vcl.h \
	hash/hash_slinger.h \
	mgt/mgt.h \
	mgt/mgt_cli.h \
	mgt/mgt_param.h \
	storage/storage.h \
	storage/storage_persistent.h \
	waiter/waiter.h

# Headers for use with vmods
pkgdataincludedir = $(pkgdatadir)/include
nobase_pkgdatainclude_HEADERS = \
	cache/cache.h \
	cache/cache_backend.h \
	common/common.h \
	common/params.h

varnishd_CFLAGS = \
	@PCRE_CFLAGS@ \
        -DVARNISH_STATE_DIR='"${VARNISH_STATE_DIR}"' \
	-DVARNISH_VMOD_DIR='"${pkglibdir}/vmods"' \
	-DVARNISH_VCL_DIR='"${varnishconfdir}"'

varnishd_LDFLAGS = -export-dynamic

varnishd_LDADD = \
	$(top_builddir)/lib/libvarnish/libvarnish.la \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvcc/libvcc.la \
	$(top_builddir)/lib/libvgz/libvgz.la \
	@JEMALLOC_LDADD@ \
	@PCRE_LIBS@ \
	${DL_LIBS} ${PTHREAD_LIBS} ${NET_LIBS} ${LIBM} ${LIBUMEM}

EXTRA_DIST = builtin.vcl
DISTCLEANFILES = builtin_vcl.h

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

# Explicitly record dependency
mgt/mgt_vcc.c:	builtin_vcl.h
