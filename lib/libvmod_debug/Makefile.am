#

INCLUDES = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/bin/varnishd \
	-I$(top_builddir)/include

vmoddir = $(pkglibdir)/vmods
vmod_srcdir = $(top_srcdir)/lib/libvmod_debug
vmodtool = $(top_srcdir)/lib/libvcl/vmodtool.py
noinst_LTLIBRARIES = libvmod_debug.la

libvmod_debug_la_LDFLAGS = -module -export-dynamic -avoid-version -rpath /nowhere

libvmod_debug_la_SOURCES = \
	vcc_if.c \
	vcc_if.h \
	vmod_debug.c

vcc_if.c vcc_if.h: $(vmodtool) $(vmod_srcdir)/vmod.vcc
	@PYTHON@ $(vmodtool) $(vmod_srcdir)/vmod.vcc

EXTRA_DIST = vmod.vcc

CLEANFILES = $(builddir)/vcc_if.c $(builddir)/vcc_if.h
