#

INCLUDES = -I$(top_srcdir)/include

pkglib_LTLIBRARIES = libvarnishcompat.la

libvarnishcompat_la_LDFLAGS = -avoid-version

libvarnishcompat_la_SOURCES = \
	daemon.c \
	execinfo.c \
	setproctitle.c \
	srandomdev.c \
	strlcpy.c \
	strndup.c
