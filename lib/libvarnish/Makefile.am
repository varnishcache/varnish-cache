# $Id$

INCLUDES = -I$(top_srcdir)/include

lib_LTLIBRARIES = libvarnish.la

libvarnish_la_SOURCES = \
	argv.c \
	assert.c \
	binary_heap.c \
	cli.c \
	cli_common.c \
	time.c \
	version.c \
	vsb.c

libvarnish_la_CFLAGS = -include config.h
