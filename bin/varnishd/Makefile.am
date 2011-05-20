#

INCLUDES = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libvgz \
	-I$(top_builddir)/include

sbin_PROGRAMS = varnishd

dist_man_MANS = varnishd.1

varnishd_SOURCES = \
	cache_acceptor.c \
	cache_waiter_epoll.c \
	cache_waiter_kqueue.c \
	cache_waiter_poll.c \
	cache_waiter_ports.c \
	cache_backend.c \
	cache_backend_cfg.c \
	cache_backend_poll.c \
	cache_ban.c \
	cache_center.c \
	cache_cli.c \
	cache_dir.c \
	cache_dir_random.c \
	cache_dir_dns.c \
	cache_dir_round_robin.c \
	cache_esi_fetch.c \
	cache_esi_parse.c \
	cache_esi_deliver.c \
	cache_expire.c \
	cache_fetch.c \
	cache_gzip.c \
	cache_hash.c \
	cache_http.c \
	cache_httpconn.c \
	cache_main.c \
	cache_lck.c \
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
	cache_wrw.c \
	cache_ws.c \
	hash_classic.c \
	hash_critbit.c \
	hash_simple_list.c \
	mgt_child.c \
	mgt_cli.c \
	mgt_param.c \
	mgt_pool.c \
	mgt_sandbox.c \
	mgt_shmem.c \
	mgt_vcc.c \
	rfc2616.c \
	stevedore.c \
	storage_file.c \
	storage_malloc.c \
	storage_persistent.c \
	storage_persistent_mgt.c \
	storage_persistent_silo.c \
	storage_persistent_subr.c \
	storage_synth.c \
	storage_umem.c \
	stevedore_utils.c \
	varnishd.c \
	vsm.c

noinst_HEADERS = \
	acct_fields.h \
	cache.h \
	cache_backend.h \
	cache_backend_poll.h \
	cache_esi.h \
	cache_waiter.h \
	common.h \
	default_vcl.h \
	hash_slinger.h \
	heritage.h \
	locks.h \
	mgt.h \
	mgt_cli.h \
	steps.h \
	stevedore.h \
	storage_persistent.h \
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
default_vcl.h:	default.vcl Makefile
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
mgt_vcc.c:	default_vcl.h

varnishd.1: $(top_srcdir)/doc/sphinx/reference/varnishd.rst
if HAVE_RST2MAN
	${RST2MAN} $? $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
