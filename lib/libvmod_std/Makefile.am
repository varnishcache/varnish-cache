# $Id$

INCLUDES = -I$(top_srcdir)/include -I$(top_builddir)/include

vmoddir = $(pkglibdir)/vmods
vmod_LTLIBRARIES = libvmod_std.la

libvmod_std_la_LDFLAGS = -version-info 1:0:0

libvmod_std_la_SOURCES = \
	vcc_if.c \
	vcc_if.h \
	vmod_std.c \
	vmod_std_fileread.c

vcc_if.c vcc_if.h: $(top_srcdir)/lib/libvmod_std/vmod.py $(top_srcdir)/lib/libvmod_std/vmod.vcc
	@PYTHON@ $(top_srcdir)/lib/libvmod_std/vmod.py $(top_srcdir)/lib/libvmod_std/vmod.vcc

EXTRA_DIST = vmod.py vmod.vcc

CLEANFILES = $(builddir)/vcc_if.c $(builddir)/vcc_if.h
