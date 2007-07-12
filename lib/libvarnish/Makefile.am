# $Id$

INCLUDES = -I$(top_srcdir)/include

lib_LTLIBRARIES = libvarnish.la

libvarnish_la_SOURCES = \
	argv.c \
	assert.c \
	binary_heap.c \
	cli.c \
	cli_common.c \
	crc32.c \
	flopen.c \
	time.c \
	version.c \
	vpf.c \
	vsb.c \
	vss.c

libvarnish_la_CFLAGS = -include config.h

libvarnish_la_LIBADD = ${RT_LIBS}
