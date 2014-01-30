#
AM_LDFLAGS  = $(AM_LT_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/bin/varnishd \
	-I$(top_builddir)/include

vmoddir = $(pkglibdir)/vmods
vmod_srcdir = $(top_srcdir)/lib/libvmod_debug
vmodtool = $(top_srcdir)/lib/libvcc/vmodtool.py
noinst_LTLIBRARIES = libvmod_debug.la

libvmod_debug_la_LDFLAGS = $(AM_LDFLAGS) -module -export-dynamic -avoid-version -shared -rpath /nowhere

libvmod_debug_la_SOURCES = \
	vmod_debug.c \
	vmod_debug_obj.c

nodist_libvmod_debug_la_SOURCES = \
	vcc_if.c \
	vcc_if.h

# BUILT_SOURCES is only a hack and dependency tracking does not help for the first build
vmod_debug.lo vmod_debug_obj.lo: vcc_if.h

vcc_if.c vcc_if.h: $(vmodtool) $(vmod_srcdir)/vmod.vcc
	@PYTHON@ $(vmodtool) $(vmod_srcdir)/vmod.vcc

EXTRA_DIST = vmod.vcc

CLEANFILES = $(builddir)/vcc_if.c $(builddir)/vcc_if.h \
	$(builddir)/vmod_debug.rst \
	$(builddir)/vmod_debug.man.rst
