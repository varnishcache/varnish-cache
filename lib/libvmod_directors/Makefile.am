#
AM_LDFLAGS  = $(AM_LT_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/bin/varnishd \
	-I$(top_builddir)/include

vmoddir = $(pkglibdir)/vmods
vmod_srcdir = $(top_srcdir)/lib/libvmod_directors
vmodtool = $(top_srcdir)/lib/libvcc/vmodtool.py
vmod_LTLIBRARIES = libvmod_directors.la

libvmod_directors_la_LDFLAGS = $(AM_LDFLAGS) -module -export-dynamic -avoid-version -shared

libvmod_directors_la_SOURCES = \
	vdir.c \
	vdir.h \
	fall_back.c \
	hash.c \
	random.c \
	round_robin.c

nodist_libvmod_directors_la_SOURCES = \
	vcc_if.c \
	vcc_if.h

# BUILT_SOURCES is only a hack and dependency tracking does not help for the first build
vdir.lo fall_back.lo hash.lo random.lo round_robin.lo: vcc_if.h

vcc_if.c vcc_if.h vmod_directors.man.rst: $(vmodtool) $(vmod_srcdir)/vmod.vcc
	@PYTHON@ $(vmodtool) $(vmod_srcdir)/vmod.vcc

EXTRA_DIST = vmod.vcc

CLEANFILES = $(builddir)/vcc_if.c $(builddir)/vcc_if.h \
	$(builddir)/vmod_directors.rst \
	$(builddir)/vmod_directors.man.rst
