# $Id$

INCLUDES = -I$(top_srcdir)/include

lib_LTLIBRARIES = libvarnishcompat.la

libvarnishcompat_la_SOURCES = \
	asprintf.c \
	vasprintf.c \
	setproctitle.c \
	srandomdev.c \
	strlcat.c \
	strlcpy.c \
	strndup.c \
	vis.c

libvarnishcompat_la_CFLAGS = -include config.h
