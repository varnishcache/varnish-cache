#

AM_LDFLAGS  = $(AM_LT_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	@PCRE_CFLAGS@

lib_LTLIBRARIES = libvarnishapi.la

libvarnishapi_la_LDFLAGS = $(AM_LDFLAGS) -version-info 1:6:0

libvarnishapi_la_SOURCES = \
	vjsn.c \
	vjsn.h \
	vsc_priv.h \
	vsl_api.h \
	vxp.h \
	vxp_tokens.h \
	../libvarnish/vas.c \
	../libvarnish/vav.c \
	../../include/vcs_version.h \
	../libvarnish/version.c \
	../libvarnish/vcli_proto.c \
	../libvarnish/vfl.c \
	../libvarnish/vin.c \
	../libvarnish/vmb.c \
	../libvarnish/vpf.c \
	../libvarnish/vre.c \
	../libvarnish/vsb.c \
	../libvarnish/vtim.c \
	../libvarnish/vnum.c \
	../libvarnish/vsha256.c \
	vsm.c \
	vsl_arg.c \
	vsl_cursor.c \
	vsl_dispatch.c \
	vsl_query.c \
	vsl.c \
	vsc.c \
	vut.c \
	vxp.c \
	vxp_parse.c \
	vxp_lexer.c \
	vxp_fixed_token.c \
	libvarnishapi.map

if ! HAVE_DAEMON
libvarnishapi_la_SOURCES += daemon.c
endif

libvarnishapi_la_CFLAGS = \
	-DVARNISH_STATE_DIR='"${VARNISH_STATE_DIR}"' \
	@SAN_CFLAGS@

libvarnishapi_la_LIBADD = \
	@SAN_LDFLAGS@ @PCRE_LIBS@ ${RT_LIBS} ${LIBM}

if HAVE_LD_VERSION_SCRIPT
libvarnishapi_la_LDFLAGS += -Wl,--version-script=$(srcdir)/libvarnishapi.map
else
libvarnishapi_la_LDFLAGS += -export-symbols-regex '^V'
endif

EXTRA_DIST = \
	generate.py

BUILT_SOURCES = \
	vxp_fixed_token.c \
	vxp_tokens.h

CLEANFILES = \
	$(builddir)/vxp_fixed_token.c \
	$(builddir)/vxp_tokens.h

noinst_PROGRAMS = vsl2rst

vsl2rst_SOURCES = \
	vsl2rst.c \
	$(top_srcdir)/include/tbl/vsl_tags.h \
	$(top_srcdir)/include/tbl/vsl_tags_http.h

vxp_fixed_token.c vxp_tokens.h: \
	$(srcdir)/generate.py
	@PYTHON@ $(srcdir)/generate.py $(srcdir) $(top_builddir)

EXTRA_PROGRAMS = vxp_test vsl_glob_test

vxp_test_LDADD = @PCRE_LIBS@ \
	${RT_LIBS} ${LIBM} ${PTHREAD_LIBS}

vxp_test_CFLAGS = \
	-DVARNISH_STATE_DIR='"${VARNISH_STATE_DIR}"' \
	-DVXP_DEBUG

vxp_test_SOURCES = \
	$(libvarnishapi_la_SOURCES) \
	vxp_test.c

vsl_glob_test_SOURCES = \
	vsl_glob_test.c

vsl_glob_test_LDADD = @PCRE_LIBS@ ${RT_LIBS} ${LIBM} libvarnishapi.la

vsl_glob_test_CFLAGS = -I$(top_srcdir)/include

TESTS = vjsn_test

noinst_PROGRAMS += ${TESTS}

vjsn_test_SOURCES = vjsn.c
vjsn_test_CFLAGS = -DVJSN_TEST @SAN_CFLAGS@
vjsn_test_LDADD = libvarnishapi.la @SAN_LDFLAGS@
