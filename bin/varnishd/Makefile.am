# $Id$

INCLUDES = -I$(top_srcdir)/include -I$(top_srcdir)/contrib/libevent

bin_PROGRAMS = varnishd

varnishd_SOURCES = \
	cache_acceptor.c \
	cache_backend.c \
	cache_fetch.c \
	cache_http.c \
	cache_main.c \
	cache_pool.c \
	cache_pass.c \
	cache_pipe.c \
	cache_shmlog.c \
	cache_vcl.c \
	cli_event.c \
	hash_simple_list.c \
	mgt_child.c \
	rfc2616.c \
	storage_file.c \
	storage_malloc.c \
	tcp.c \
	varnishd.c

varnishd_LDFLAGS = -export-dynamic 

varnishd_LDADD = \
	$(top_builddir)/lib/libcompat/libcompat.a \
	$(top_builddir)/lib/libsbuf/libsbuf.a \
	$(top_builddir)/lib/libvarnish/libvarnish.la \
	$(top_builddir)/lib/libvcl/libvcl.la \
	$(top_builddir)/contrib/libevent/libevent.la \
	-lpthread \
	-lmd
