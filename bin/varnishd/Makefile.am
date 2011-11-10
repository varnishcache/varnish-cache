#

INCLUDES = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libvgz \
	-I$(top_builddir)/include

sbin_PROGRAMS = varnishd

dist_man_MANS = varnishd.1

varnishd_SOURCES = \
	cache_acceptor.c \
	cache_backend.c \
	cache_backend_cfg.c \
	cache_backend_poll.c \
	cache_ban.c \
	cache_center.c \
	cache_cli.c \
	cache_dir.c \
	cache_dir_dns.c \
	cache_dir_random.c \
	cache_dir_round_robin.c \
	cache_esi_deliver.c \
	cache_esi_fetch.c \
	cache_esi_parse.c \
	cache_expire.c \
	cache_fetch.c \
	cache_gzip.c \
	cache_hash.c \
	cache_http.c \
	cache_httpconn.c \
	cache_lck.c \
	cache_main.c \
	cache_panic.c \
	cache_pipe.c \
	cache_pool.c \
	cache_response.c \
	cache_session.c \
	cache_shmlog.c \
	cache_vary.c \
	cache_vcl.c \
	cache_vrt.c \
	cache_vrt_re.c \
	cache_vrt_var.c \
	cache_vrt_vmod.c \
	waiter/cache_waiter.c \
	waiter/cache_waiter_epoll.c \
	waiter/cache_waiter_kqueue.c \
	waiter/cache_waiter_poll.c \
	waiter/cache_waiter_ports.c \
	cache_wrk.c \
	cache_wrw.c \
	cache_ws.c \
	hash/hash_classic.c \
	hash/hash_critbit.c \
	hash/hash_mgt.c \
	hash/hash_simple_list.c \
	mgt/mgt_child.c \
	mgt/mgt_cli.c \
	mgt/mgt_param.c \
	mgt/mgt_pool.c \
	mgt/mgt_sandbox.c \
	mgt/mgt_sandbox_solaris.c \
	mgt/mgt_shmem.c \
	mgt/mgt_vcc.c \
	rfc2616.c \
	storage/stevedore.c \
	storage/stevedore_utils.c \
	storage/storage_file.c \
	storage/storage_malloc.c \
	storage/storage_persistent.c \
	storage/storage_persistent_mgt.c \
	storage/storage_persistent_silo.c \
	storage/storage_persistent_subr.c \
	storage/storage_synth.c \
	storage/storage_umem.c \
	varnishd.c \
	vsm.c

noinst_HEADERS = \
	cache.h \
	cache_backend.h \
	cache_esi.h \
	waiter/cache_waiter.h \
	common.h \
	default_vcl.h \
	hash/hash_slinger.h \
	heritage.h \
	mgt/mgt.h \
	mgt/mgt_cli.h \
	storage/storage.h \
	storage/storage_persistent.h \
	vparam.h

varnishd_CFLAGS = \
	@PCRE_CFLAGS@ \
        -DVARNISH_STATE_DIR='"${VARNISH_STATE_DIR}"' \
	-DVARNISH_VMOD_DIR='"${pkglibdir}/vmods"' \
	-DVARNISH_VCL_DIR='"${varnishconfdir}"'

varnishd_LDFLAGS = -export-dynamic

varnishd_LDADD = \
	$(top_builddir)/lib/libvarnish/libvarnish.la \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvcl/libvcl.la \
	$(top_builddir)/lib/libvgz/libvgz.la \
	@JEMALLOC_LDADD@ \
	@PCRE_LIBS@ \
	${DL_LIBS} ${PTHREAD_LIBS} ${NET_LIBS} ${LIBM} ${LIBUMEM}

EXTRA_DIST = default.vcl
DISTCLEANFILES = default_vcl.h

#
# Turn the default.vcl file into a C-string we can include in the program.
#
default_vcl.h:	default.vcl
	echo '/*' > $@
	echo ' * NB:  This file is machine generated, DO NOT EDIT!' >> $@
	echo ' *' >> $@
	echo ' * Edit default.vcl instead and run make' >> $@
	echo ' *' >> $@
	echo ' */' >> $@
	echo '' >> $@
	sed -e 's/"/\\"/g' \
	    -e 's/$$/\\n"/' \
	    -e 's/^/ "/' $(srcdir)/default.vcl >> $@

# Explicitly record dependency
mgt/mgt_vcc.c:	default_vcl.h

varnishd.1: $(top_srcdir)/doc/sphinx/reference/varnishd.rst
if HAVE_RST2MAN
	${RST2MAN} $? $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
