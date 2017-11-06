#

libvmod_debug_la_SOURCES = \
	vmod_debug.c \
	vmod_debug_obj.c \
	vmod_debug_dyn.c

# Boilerplate, do not change

AM_LDFLAGS  = $(AM_LT_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/bin/varnishd \
	-I$(top_builddir)/include

vmoddir = $(pkglibdir)/vmods
vmod_srcdir = $(top_srcdir)/lib/libvmod_debug
vmodtool = $(top_srcdir)/lib/libvcc/vmodtool.py
vmodtoolargs =


noinst_LTLIBRARIES = libvmod_debug.la

libvmod_debug_la_CFLAGS = \
	@SAN_CFLAGS@

libvmod_debug_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	$(VMOD_LDFLAGS) \
	-rpath /nowhere
	@SAN_LDFLAGS@

nodist_libvmod_debug_la_SOURCES = \
	vcc_if.c \
	vcc_if.h

$(libvmod_debug_la_OBJECTS): vcc_if.h

vcc_if.h vmod_debug.rst vmod_debug.man.rst: vcc_if.c

vcc_if.c: $(vmodtool) $(vmod_srcdir)/vmod.vcc
	@PYTHON@ $(vmodtool) $(vmodtoolargs) $(vmod_srcdir)/vmod.vcc

.vsc.c:
	$(PYTHON) $(top_srcdir)/lib/libvcc/vsctool.py -ch $<

VSC_SRC		= VSC_debug.vsc
VSC_GEN_C	= VSC_debug.c
VSC_GEN_H	= VSC_debug.h

BUILT_SOURCES			 = $(VSC_GEN_C)
libvmod_debug_la_SOURCES	+= $(VSC_SRC)

$(VSC_GEN_C): $(top_srcdir)/lib/libvcc/vsctool.py

EXTRA_DIST = vmod.vcc

CLEANFILES = $(builddir)/vcc_if.c $(builddir)/vcc_if.h \
	$(builddir)/vmod_debug.rst \
	$(builddir)/vmod_debug.man.rst \
	$(VSC_GEN_C) $(VSC_GEN_H)
