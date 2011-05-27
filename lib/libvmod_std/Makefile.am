#

INCLUDES = -I$(top_srcdir)/include -I$(top_builddir)/include

dist_man_MANS = vmod_std.3

vmoddir = $(pkglibdir)/vmods
vmod_LTLIBRARIES = libvmod_std.la

libvmod_std_la_LDFLAGS = -avoid-version

libvmod_std_la_SOURCES = \
	vcc_if.c \
	vcc_if.h \
	vmod_std.c \
	vmod_std_fileread.c \
	vmod_std_conversions.c

vcc_if.c vcc_if.h: $(top_srcdir)/lib/libvmod_std/vmod.py $(top_srcdir)/lib/libvmod_std/vmod.vcc
	@PYTHON@ $(top_srcdir)/lib/libvmod_std/vmod.py $(top_srcdir)/lib/libvmod_std/vmod.vcc

EXTRA_DIST = vmod.py vmod.vcc

CLEANFILES = $(builddir)/vcc_if.c $(builddir)/vcc_if.h

vmod_std.3: $(top_srcdir)/doc/sphinx/reference/vmod_std.rst
if HAVE_RST2MAN
	${RST2MAN} $? $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
