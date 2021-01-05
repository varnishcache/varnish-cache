#

AM_LDFLAGS  = $(AM_LT_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/bin/varnishd \
	-I$(top_builddir)/include

vmod_LTLIBRARIES =
EXTRA_DIST =
CLEANFILES =

libvmod_directors_la_SOURCES = \
	vdir.c \
	vdir.h \
	fall_back.c \
	hash.c \
	misc.c \
	random.c \
	round_robin.c \
	vmod_shard.c \
	shard_cfg.c \
	shard_cfg.h \
	shard_dir.c \
	shard_dir.h

# Use vmodtool.py generated automake boilerplate
include $(srcdir)/automake_boilerplate_directors.am
