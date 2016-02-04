#

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include

AM_LDFLAGS  = $(AM_LT_LDFLAGS)

pkglib_LTLIBRARIES = libvarnishcompat.la

libvarnishcompat_la_CFLAGS = \
	@ASAN_CFLAGS@ \
	@MSAN_CFLAGS@ \
	@TSAN_CFLAGS@ \
	@UBSAN_CFLAGS@

libvarnishcompat_la_LDFLAGS = $(AM_LDFLAGS) -avoid-version \
	@ASAN_LDFLAGS@ \
	@MSAN_LDFLAGS@ \
	@TSAN_LDFLAGS@ \
	@UBSAN_LDFLAGS@

libvarnishcompat_la_SOURCES = \
	daemon.c \
	execinfo.c
