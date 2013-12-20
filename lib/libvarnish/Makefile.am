AM_CPPFLAGS = -I$(top_srcdir)/include @PCRE_CFLAGS@

AM_LDFLAGS  = $(AM_LT_LDFLAGS)

pkglib_LTLIBRARIES = libvarnish.la

libvarnish_la_LDFLAGS = $(AM_LDFLAGS) -avoid-version

libvarnish_la_SOURCES = \
	vav.c \
	vas.c \
	binary_heap.c \
	vsub.c \
	cli_auth.c \
	cli_common.c \
	cli_serve.c \
	flopen.c \
	vnum.c \
	vtim.c \
	vtcp.c \
	vct.c \
	version.c \
	vev.c \
	vfil.c \
	vin.c \
	vlu.c \
	vmb.c \
	vpf.c \
	vre.c \
	vrnd.c \
	vsa.c \
	vsb.c \
	vsha256.c \
	vss.c

libvarnish_la_CFLAGS = -DVARNISH_STATE_DIR='"${VARNISH_STATE_DIR}"'
libvarnish_la_LIBADD = ${RT_LIBS} ${NET_LIBS} ${LIBM} @PCRE_LIBS@

if ENABLE_TESTS
TESTS = vnum_c_test

noinst_PROGRAMS = ${TESTS}

vnum_c_test_SOURCES = vnum.c
vnum_c_test_CFLAGS = -DNUM_C_TEST -include config.h
vnum_c_test_LDADD = ${LIBM}

test: ${TESTS}
	@for test in ${TESTS} ; do ./$${test} ; done
endif
