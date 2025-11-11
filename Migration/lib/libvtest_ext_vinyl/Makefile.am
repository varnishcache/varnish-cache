#

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	@PCRE2_CFLAGS@

AM_CFLAGS   = $(AM_LT_CFLAGS)
AM_LDFLAGS  = $(AM_LT_LDFLAGS)

lib_LTLIBRARIES = libvtest_ext_vinyl.la

libvtest_ext_vinyl_la_LIBADD = \
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la

libvtest_ext_vinyl_la_CFLAGS = \
	-DVARNISH_STATE_DIR='"${VARNISH_STATE_DIR}"' \
	-DVTEST_WITH_VTC_VSM=1 \
	-DVTEST_WITH_VTC_VARNISH=1 \
	-DVTEST_WITH_VTC_LOGEXPECT=1 \
	$(AM_CFLAGS)

libvtest_ext_vinyl_la_LDFLAGS = $(AM_LDFLAGS)

libvtest_ext_vinyl_la_SOURCES = \
	vtc_logexp.c \
	vtc_varnish.c \
	vtc_vsm.c \
	vtest_ext_vinyl.c


