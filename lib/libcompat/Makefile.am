# $Id$

INCLUDES = -I$(top_srcdir)/include

noinst_LIBRARIES = libcompat.a

libcompat_a_SOURCES = \
	asprintf.c \
	clock_gettime.c \
	vasprintf.c \
	setproctitle.c \
	srandomdev.c \
	strlcat.c \
	strlcpy.c \
	strndup.c \
	vis.c

libcompat_a_CFLAGS = -include config.h
