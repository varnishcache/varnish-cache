#

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishreplay

dist_man_MANS = varnishreplay.1

varnishreplay_SOURCES = \
	varnishreplay.c \
	$(top_builddir)/lib/libvarnish/assert.c \
	$(top_builddir)/lib/libvarnish/tcp.c \
	$(top_builddir)/lib/libvarnish/vss.c

varnishreplay_LDADD = \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	${PTHREAD_LIBS} ${NET_LIBS} ${LIBM}

varnishreplay.1: $(top_srcdir)/doc/sphinx/reference/varnishreplay.rst
if HAVE_RST2MAN
	${RST2MAN} $? $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
