AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	@PCRE_CFLAGS@

AM_LDFLAGS  = $(AM_LT_LDFLAGS)

pkglib_LTLIBRARIES = libvarnish.la

libvarnish_la_LDFLAGS = $(AM_LDFLAGS) -avoid-version

libvarnish_la_SOURCES = \
	binary_heap.c \
	vas.c \
	vav.c \
	vcli_proto.c \
	vcli_serve.c \
	vct.c \
	version.c \
	vev.c \
	vfil.c \
	vfl.c \
	vin.c \
	vlu.c \
	vmb.c \
	vnum.c \
	vpf.c \
	vre.c \
	vrnd.c \
	vsa.c \
	vsb.c \
	vsha256.c \
	vss.c \
	vsub.c \
	vtcp.c \
	vtim.c \
	ws.c

libvarnish_la_CFLAGS = \
	-DVARNISH_STATE_DIR='"${VARNISH_STATE_DIR}"' \
	@SAN_CFLAGS@
libvarnish_la_LIBADD = ${RT_LIBS} ${NET_LIBS} ${LIBM} @PCRE_LIBS@ \
	@SAN_LDFLAGS@

TESTS = vnum_c_test

noinst_PROGRAMS = ${TESTS}

vnum_c_test_SOURCES = vnum.c vas.c
vnum_c_test_CFLAGS = -DNUM_C_TEST -include config.h
vnum_c_test_LDADD = ${LIBM}

test: ${TESTS}
	@for test in ${TESTS} ; do ./$${test} ; done
