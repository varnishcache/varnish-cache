#

AM_CPPFLAGS = -I$(top_srcdir)/include

bin_PROGRAMS = varnishlog

dist_man_MANS = varnishlog.1

varnishlog_SOURCES = \
	varnishlog.c \
	vut.c \
	vut.h \
	$(top_builddir)/lib/libvarnish/vas.c \
	$(top_builddir)/lib/libvarnish/flopen.c \
	$(top_builddir)/lib/libvarnish/version.c \
	$(top_builddir)/lib/libvarnish/vsb.c \
	$(top_builddir)/lib/libvarnish/vpf.c \
	$(top_builddir)/lib/libvarnish/vtim.c

varnishlog_LDADD = \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	${RT_LIBS} ${LIBM} ${PTHREAD_LIBS}

varnishlog.1: $(top_srcdir)/doc/sphinx/reference/varnishlog.rst
if HAVE_RST2MAN
	${RST2MAN} $? $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
