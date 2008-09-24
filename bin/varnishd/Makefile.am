# $Id$

INCLUDES = -I$(top_srcdir)/include

sbin_PROGRAMS = varnishd

dist_man_MANS = varnishd.1

varnishd_SOURCES = \
	cache_acceptor.c \
	cache_acceptor_epoll.c \
	cache_acceptor_kqueue.c \
	cache_acceptor_poll.c \
	cache_acceptor_ports.c \
	cache_backend.c \
	cache_backend_cfg.c \
	cache_backend_poll.c \
	cache_ban.c \
	cache_center.c \
	cache_cli.c \
	cache_dir_random.c \
	cache_dir_round_robin.c \
	cache_dir_simple.c \
	cache_expire.c \
	cache_fetch.c \
	cache_hash.c \
	cache_http.c \
	cache_httpconn.c \
	cache_main.c \
	cache_panic.c \
	cache_pipe.c \
	cache_pool.c \
	cache_response.c \
	cache_session.c \
	cache_vary.c \
	cache_vcl.c \
	cache_vrt.c \
	cache_vrt_acl.c \
	cache_vrt_esi.c \
	cache_vrt_re.c \
	cache_ws.c \
	hash_classic.c \
	hash_simple_list.c \
	instance.c \
	mgt_child.c \
	mgt_cli.c \
	mgt_param.c \
	mgt_vcc.c \
	rfc2616.c \
	shmlog.c \
	stevedore.c \
	storage_file.c \
	storage_malloc.c \
	storage_synth.c \
	storage_umem.c \
	varnishd.c

noinst_HEADERS = \
	acct_fields.h \
	cache.h \
	cache_acceptor.h \
	cache_backend.h \
	cache_backend_poll.h \
	common.h \
	default_vcl.h \
	hash_slinger.h \
	heritage.h \
	mgt.h \
	mgt_cli.h \
	steps.h \
	stevedore.h

varnishd_CFLAGS = \
        -DVARNISH_STATE_DIR='"${VARNISH_STATE_DIR}"'

varnishd_LDFLAGS = -export-dynamic

varnishd_LDADD = \
	$(top_builddir)/lib/libvarnish/libvarnish.la \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvcl/libvcl.la \
	@JEMALLOC_LDADD@ \
	${DL_LIBS} ${PTHREAD_LIBS} ${NET_LIBS} ${LIBM}

EXTRA_DIST = default.vcl
DISTCLEANFILES = default_vcl.h

#
# Turn the default.vcl file into a C-string we can include in the program.
#
default_vcl.h:	default.vcl Makefile
	sed -e 's/"/\\"/g' -e 's/$$/\\n"/' -e 's/^/	"/' $(srcdir)/default.vcl > $@

# Explicitly record dependency
mgt_vcc.c:	default_vcl.h

