# $Id$

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishadm

if HAVE_RST2MAN
dist_man_MANS = varnishadm.1
endif

varnishadm_SOURCES = \
	varnishadm.c

varnishadm_LDADD = \
	$(top_builddir)/lib/libvarnish/libvarnish.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	${PTHREAD_LIBS} ${NET_LIBS}

%.1: ../../doc/sphinx/reference/%.rst
if HAVE_RST2MAN
	${RST2MAN} $< >$@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
