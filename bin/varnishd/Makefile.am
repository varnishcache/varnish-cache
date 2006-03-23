# $Id$

INCLUDES = -I$(top_srcdir)/include -I$(top_srcdir)/contrib/libevent

bin_PROGRAMS = varnishd

varnishd_SOURCES = \
	cache_main.c \
	cli_event.c \
	mgt_child.c \
	tcp.c \
	varnishd.c

varnishd_LDADD = \
	$(top_builddir)/lib/libvarnish/libvarnish.la \
	$(top_builddir)/lib/libsbuf/libsbuf.la \
	$(top_builddir)/contrib/libevent/libevent.la
