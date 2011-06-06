#

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishncsa

dist_man_MANS = varnishncsa.1

varnishncsa_SOURCES = \
	varnishncsa.c \
	base64.c \
	$(top_builddir)/lib/libvarnish/flopen.c \
	$(top_builddir)/lib/libvarnish/vpf.c

varnishncsa_LDADD = \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	${PTHREAD_LIBS}

varnishncsa.1: $(top_srcdir)/doc/sphinx/reference/varnishncsa.rst
if HAVE_RST2MAN
	${RST2MAN} $? $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
