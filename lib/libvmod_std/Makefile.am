# $Id$

INCLUDES = -I$(top_srcdir)/include

lib_LTLIBRARIES = libvmod_std.la

libvmod_std_la_LDFLAGS = -version-info 1:0:0

libvmod_std_la_SOURCES = \
	$(builddir)/vmod.c \
	vmod_std.c

$(builddir)/vmod.c $(builddir)/vmod.h: $(top_srcdir)/lib/libvmod_std/vmod.py $(top_srcdir)/lib/libvmod_std/vmod.vcc
	@PYTHON@ $(top_srcdir)/lib/libvmod_std/vmod.py $(top_srcdir)/lib/libvmod_std/vmod.vcc

EXTRA_DIST = vmod.py vmod.vcc

CLEANFILES = $(builddir)/vmod.c $(builddir)/vmod.h
