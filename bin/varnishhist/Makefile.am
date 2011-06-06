#

INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = varnishhist

dist_man_MANS = varnishhist.1

varnishhist_SOURCES = varnishhist.c \
	$(top_builddir)/lib/libvarnish/version.c

varnishhist_LDADD = \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	-lm \
	${CURSES_LIBS} ${PTHREAD_LIBS}

varnishhist.1: $(top_srcdir)/doc/sphinx/reference/varnishhist.rst
if HAVE_RST2MAN
	${RST2MAN} $? $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
