# $Id$

INCLUDES = -I$(top_srcdir)/include

lib_LIBRARIES = libcompat.a

libcompat_a_SOURCES =

if !HAVE_STRLCAT
libcompat_a_SOURCES += strlcat.c
endif

if !HAVE_STRLCPY
libcompat_a_SOURCES += strlcpy.c
endif
