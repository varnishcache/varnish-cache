#

AM_LDFLAGS  = $(AM_LT_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/bin/varnishd \
	-I$(top_builddir)/include

vmoddir = $(pkglibdir)/vmods
vmod_srcdir = $(top_srcdir)/lib/libvmod_std
vmodtool = $(top_srcdir)/lib/libvcc/vmodtool.py
vmod_LTLIBRARIES = libvmod_std.la

libvmod_std_la_LDFLAGS = $(AM_LDFLAGS) -module -export-dynamic -avoid-version -shared

libvmod_std_la_SOURCES = \
	vmod_std.c \
	vmod_std_fileread.c \
	vmod_std_conversions.c

nodist_libvmod_std_la_SOURCES = \
	vcc_if.c \
	vcc_if.h

# BUILT_SOURCES is only a hack and dependency tracking does not help for the first build
vmod_std.lo vmod_std_fileread.lo vmod_std_conversions.lo: vcc_if.h

vcc_if.c vcc_if.h vmod_std.man.rst: $(vmodtool) $(vmod_srcdir)/vmod.vcc
	@PYTHON@ $(vmodtool) $(vmod_srcdir)/vmod.vcc

EXTRA_DIST = vmod.vcc

CLEANFILES = $(builddir)/vcc_if.c $(builddir)/vcc_if.h \
	$(builddir)/vmod_std.rst \
	$(builddir)/vmod_std.man.rst
