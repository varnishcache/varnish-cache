#

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishlog

dist_man_MANS = varnishlog.1

varnishlog_SOURCES = varnishlog.c

varnishlog_LDADD = \
	$(top_builddir)/lib/libvarnish/libvarnish.la \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	${PTHREAD_LIBS}

varnishlog.1: $(top_srcdir)/doc/sphinx/reference/varnishlog.rst
if HAVE_RST2MAN
	${RST2MAN} $? $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
