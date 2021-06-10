#

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	@PCRE_CFLAGS@

AM_CFLAGS   = $(AM_LT_CFLAGS)  @SAN_CFLAGS@
AM_LDFLAGS  = $(AM_LT_LDFLAGS) @SAN_LDFLAGS@

noinst_LTLIBRARIES = libvarnish.la

libvarnish_la_CFLAGS = \
	-DVARNISH_STATE_DIR='"${VARNISH_STATE_DIR}"' \
	$(AM_CFLAGS)

libvarnish_la_SOURCES = \
	vbh.c \
	vas.c \
	vav.c \
	vcli_proto.c \
	vcli_serve.c \
	vct.c \
	venc.c \
	version.c \
	vev.c \
	vfil.c \
	vfl.c \
	vin.c \
	vjsn.c \
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
	vte.c \
	vtim.c \
	vus.c

libvarnish_la_LIBADD = @PCRE_LIBS@

TESTS = vjsn_test vnum_c_test vbh_test vsb_test

noinst_PROGRAMS = ${TESTS}

vbh_test_SOURCES	= vbh.c
vbh_test_CFLAGS = $(AM_CFLAGS) -DTEST_DRIVER
vbh_test_LDADD = $(AM_LDFLAGS) libvarnish.la

vnum_c_test_SOURCES = vnum.c
vnum_c_test_CFLAGS = $(AM_CFLAGS) -DNUM_C_TEST
vnum_c_test_LDADD = $(AM_LDFLAGS) libvarnish.la ${LIBM}

vjsn_test_SOURCES = vjsn.c
vjsn_test_CFLAGS = $(AM_CFLAGS) -DVJSN_TEST
vjsn_test_LDADD = $(AM_LDFLAGS) libvarnish.la

vsb_test_SOURCES = vsb_test.c
vsb_test_CFLAGS = $(AM_CFLAGS) -DVSB_TEST
vsb_test_LDADD = $(AM_LDFLAGS) libvarnish.la
