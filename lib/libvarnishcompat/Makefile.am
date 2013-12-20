#

AM_CPPFLAGS = -I$(top_srcdir)/include
AM_LDFLAGS  = $(AM_LT_LDFLAGS)

pkglib_LTLIBRARIES = libvarnishcompat.la

libvarnishcompat_la_LDFLAGS = $(AM_LDFLAGS) -avoid-version

libvarnishcompat_la_SOURCES = \
	daemon.c \
	execinfo.c
