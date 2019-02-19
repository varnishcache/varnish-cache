#

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	@PCRE_CFLAGS@

AM_LDFLAGS  = $(AM_LT_LDFLAGS)

noinst_LIBRARIES = libvarnish.a

libvarnish_a_CFLAGS = \
	-DVARNISH_STATE_DIR='"${VARNISH_STATE_DIR}"' \
	@SAN_CFLAGS@

libvarnish_a_SOURCES = \
	binary_heap.c \
	vas.c \
	vav.c \
	vcli_proto.c \
	vcli_serve.c \
	vct.c \
	verrno.c \
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

TESTS = vjsn_test vnum_c_test binheap

noinst_PROGRAMS = ${TESTS}

binheap_SOURCES = binary_heap.c vas.c vrnd.c
binheap_CFLAGS = -DTEST_DRIVER

vnum_c_test_SOURCES = vnum.c vas.c
vnum_c_test_CFLAGS = -DNUM_C_TEST -include config.h @SAN_CFLAGS@
vnum_c_test_LDADD = ${LIBM} libvarnish.a @SAN_LDFLAGS@

vjsn_test_SOURCES = vjsn.c
vjsn_test_CFLAGS = -DVJSN_TEST @SAN_CFLAGS@
vjsn_test_LDADD = libvarnish.a @SAN_LDFLAGS@
