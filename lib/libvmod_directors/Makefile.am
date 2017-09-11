#

libvmod_directors_la_SOURCES = \
	vdir.c \
	vdir.h \
	fall_back.c \
	hash.c \
	random.c \
	round_robin.c \
	vmod_shard.c \
	shard_cfg.c \
	shard_cfg.h \
	shard_dir.c \
	shard_dir.h \
	shard_hash.c \
	shard_hash.h \
	shard_parse_vcc_enums.h \
	shard_parse_vcc_enums.c

# Boilerplate, do not change

AM_LDFLAGS  = $(AM_LT_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/bin/varnishd \
	-I$(top_builddir)/include

vmoddir = $(pkglibdir)/vmods
vmod_srcdir = $(top_srcdir)/lib/libvmod_directors
vmodtool = $(top_srcdir)/lib/libvcc/vmodtool.py
vmodtoolargs = --strict

vmod_LTLIBRARIES = libvmod_directors.la

libvmod_directors_la_CFLAGS = \
	@SAN_CFLAGS@

libvmod_directors_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	-module \
	-export-dynamic \
	-avoid-version \
	-shared \
	-export-symbols-regex '^Vmod_std_Data' \
	-rpath /nowhere \
	@SAN_LDFLAGS@

nodist_libvmod_directors_la_SOURCES = \
	vcc_if.c \
	vcc_if.h

# BUILT_SOURCES is only a hack and dependency tracking does not help for the first build
$(libvmod_directors_la_OBJECTS): vcc_if.h

vcc_if.h vmod_directors.rst vmod_directors.man.rst: vcc_if.c

vcc_if.c: $(vmodtool) $(vmod_srcdir)/vmod.vcc
	@PYTHON@ $(vmodtool) $(vmodtoolargs) $(vmod_srcdir)/vmod.vcc

EXTRA_DIST = vmod.vcc

CLEANFILES = $(builddir)/vcc_if.c $(builddir)/vcc_if.h \
	$(builddir)/vmod_directors.rst \
	$(builddir)/vmod_directors.man.rst
