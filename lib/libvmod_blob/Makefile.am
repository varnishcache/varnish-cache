#

libvmod_blob_la_SOURCES = \
	vmod_blob.c \
	vmod_blob.h \
	id.c \
	base64.h \
	base64.c \
	hex.h \
	hex.c \
	url.c \
	wb.h \
	wb.c \
	parse_encoding.h \
	parse_encoding.c

base64.o: base64.c base64.h

# Boilerplate, do not change

AM_LDFLAGS  = $(AM_LT_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/bin/varnishd \
	-I$(top_builddir)/include

vmoddir = $(pkglibdir)/vmods
vmod_srcdir = $(top_srcdir)/lib/libvmod_blob
vmodtool = $(top_srcdir)/lib/libvcc/vmodtool.py
vmodtoolargs = --strict

vmod_LTLIBRARIES = libvmod_blob.la

libvmod_blob_la_CFLAGS = \
	@SAN_CFLAGS@

libvmod_blob_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	-module \
	-export-dynamic \
	-avoid-version \
	-shared \
	-export-symbols-regex '^Vmod_std_Data' \
	-rpath /nowhere \
	@SAN_LDFLAGS@

nodist_libvmod_blob_la_SOURCES = \
	vcc_if.c \
	vcc_if.h

# BUILT_SOURCES is only a hack and dependency tracking does not help for the first build
$(libvmod_blob_la_OBJECTS): vcc_if.h

vcc_if.h vmod_blob.rst vmod_blob.man.rst: vcc_if.c

vcc_if.c: $(vmodtool) $(vmod_srcdir)/vmod.vcc
	@PYTHON@ $(vmodtool) $(vmodtoolargs) $(vmod_srcdir)/vmod.vcc

EXTRA_DIST = vmod.vcc

CLEANFILES = $(builddir)/vcc_if.c $(builddir)/vcc_if.h \
	$(builddir)/vmod_blob.rst \
	$(builddir)/vmod_blob.man.rst
