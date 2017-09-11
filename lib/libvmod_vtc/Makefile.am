#

libvmod_vtc_la_SOURCES = \
	vmod_vtc.c

# Boilerplate, do not change

AM_LDFLAGS  = $(AM_LT_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/bin/varnishd \
	-I$(top_builddir)/include

vmoddir = $(pkglibdir)/vmods
vmod_srcdir = $(top_srcdir)/lib/libvmod_vtc
vmodtool = $(top_srcdir)/lib/libvcc/vmodtool.py
vmodtoolargs = --strict

vmod_LTLIBRARIES = libvmod_vtc.la

libvmod_vtc_la_CFLAGS = \
	@SAN_CFLAGS@

libvmod_vtc_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	-module \
	-export-dynamic \
	-avoid-version \
	-shared \
	-rpath /nowhere \
	@SAN_LDFLAGS@

nodist_libvmod_vtc_la_SOURCES = \
	vcc_if.c \
	vcc_if.h

# BUILT_SOURCES is only a hack and dependency tracking does not help for the first build
$(libvmod_vtc_la_OBJECTS): vcc_if.h

vcc_if.h vmod_vtc.rst vmod_vtc.man.rst: vcc_if.c

vcc_if.c: $(vmodtool) $(vmod_srcdir)/vmod.vcc
	@PYTHON@ $(vmodtool) $(vmodtoolargs) $(vmod_srcdir)/vmod.vcc

EXTRA_DIST = vmod.vcc

CLEANFILES = $(builddir)/vcc_if.c $(builddir)/vcc_if.h \
	$(builddir)/vmod_vtc.rst \
	$(builddir)/vmod_vtc.man.rst
