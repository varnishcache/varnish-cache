#

AM_CPPFLAGS = -I$(top_srcdir)/include

bin_PROGRAMS = varnishhist

varnishhist_SOURCES = varnishhist.c \
	varnishhist_options.h \
	varnishhist_options.c \
	$(top_builddir)/lib/libvarnish/vas.c \
	$(top_builddir)/lib/libvarnish/version.c \
	$(top_srcdir)/lib/libvarnish/vpf.c \
	$(top_srcdir)/lib/libvarnish/vtim.c \
	$(top_srcdir)/lib/libvarnish/flopen.c \
	$(top_srcdir)/lib/libvarnishtools/vut.c

varnishhist_LDADD = \
	$(top_builddir)/lib/libvarnishcompat/libvarnishcompat.la \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
	-lm \
	@CURSES_LIB@ ${RT_LIBS} ${PTHREAD_LIBS}

noinst_PROGRAMS = varnishhist_opt2rst
varnishhist_opt2rst_SOURCES = \
	varnishhist_options.h \
	varnishhist_options.c \
	$(top_srcdir)/lib/libvarnishtools/opt2rst.c

BUILT_SOURCES = varnishhist_options.rst varnishhist_synopsis.rst
EXTRA_DIST = $(BUILT_SOURCES)
MAINTAINERCLEANFILES = $(EXTRA_DIST)

varnishhist_options.rst:
	./varnishhist_opt2rst options > $@
varnishhist_synopsis.rst:
	./varnishhist_opt2rst synopsis > $@

if HAVE_RST2MAN
varnishhist_options.rst varnishhist_synopsis.rst: varnishhist_opt2rst
endif

varnishhist.1: \
	$(top_srcdir)/doc/sphinx/reference/varnishhist.rst \
	varnishhist_options.rst \
	varnishhist_synopsis.rst
if HAVE_RST2MAN
	${RST2MAN} $(top_srcdir)/doc/sphinx/reference/varnishhist.rst $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
