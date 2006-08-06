# $Id$

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishd

dist_man_MANS = varnishd.1

varnishd_SOURCES = \
	cache.h \
	common_cli.h \
	hash_slinger.h \
	heritage.h \
	mgt.h \
	mgt_cli.h \
	mgt_event.h \
	stevedore.h \
	\
	cache_acceptor.c \
	cache_backend.c \
	cache_ban.c \
	cache_center.c \
	cache_cli.c \
	cache_expire.c \
	cache_fetch.c \
	cache_hash.c \
	cache_http.c \
	cache_main.c \
	cache_pool.c \
	cache_pass.c \
	cache_pipe.c \
	cache_response.c \
	cache_session.c \
	cache_vcl.c \
	cache_vrt.c \
	cache_vrt_acl.c \
	cache_vrt_re.c \
	common_cli.c \
	hash_simple_list.c \
	hash_classic.c \
	mgt_child.c \
	mgt_cli.c \
	mgt_event.c \
	mgt_vcc.c \
	rfc2616.c \
	shmlog.c \
	storage_file.c \
	storage_malloc.c \
	tcp.c \
	varnishd.c

noinst_HEADERS = \
	common.h \
	steps.h

varnishd_CFLAGS = -include config.h

varnishd_LDFLAGS = -export-dynamic 

varnishd_LDADD = \
	$(top_builddir)/lib/libcompat/libcompat.a \
	$(top_builddir)/lib/libsbuf/libsbuf.a \
	$(top_builddir)/lib/libvarnish/libvarnish.la \
	$(top_builddir)/lib/libvcl/libvcl.la \
	-lpthread \
	-lmd
