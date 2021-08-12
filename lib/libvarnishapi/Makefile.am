#

AM_LDFLAGS  = $(AM_LT_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include

lib_LTLIBRARIES = libvarnishapi.la

libvarnishapi_la_LDFLAGS = $(AM_LDFLAGS) -version-info 2:0:0

libvarnishapi_la_SOURCES = \
	../../include/vcs_version.h \
	vsl_api.h \
	vxp.h \
	vxp_tokens.h \
	vsc.c \
	vsig.c \
	vsl.c \
	vsl_arg.c \
	vsl_cursor.c \
	vsl_dispatch.c \
	vsl_query.c \
	vsm.c \
	vut.c \
	vxp.c \
	vxp_fixed_token.c \
	vxp_lexer.c \
	vxp_parse.c \
	libvarnishapi.map

if ! HAVE_DAEMON
libvarnishapi_la_SOURCES += daemon.c
endif

libvarnishapi_la_CFLAGS = \
	-DVARNISH_STATE_DIR='"${VARNISH_STATE_DIR}"' \
	@SAN_CFLAGS@

libvarnishapi_la_LIBADD = \
	$(top_builddir)/lib/libvarnish/libvarnish.la \
	@SAN_LDFLAGS@ ${NET_LIBS} ${RT_LIBS} ${LIBM}

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

vxp_fixed_token.c: vxp_tokens.h

vxp_tokens.h: \
	$(srcdir)/generate.py
	@PYTHON@ $(srcdir)/generate.py $(srcdir) $(top_builddir)

noinst_PROGRAMS += vxp_test

vxp_test_SOURCES = \
	$(libvarnishapi_la_SOURCES) \
	vxp_test.c
vxp_test_CFLAGS = \
	-DVARNISH_STATE_DIR='"${VARNISH_STATE_DIR}"' \
	-DVXP_DEBUG \
	${SAN_CFLAGS}
vxp_test_LDADD = \
	$(top_builddir)/lib/libvarnish/libvarnish.la \
	${RT_LIBS} ${LIBM} ${PTHREAD_LIBS} ${SAN_LDFLAGS}

noinst_PROGRAMS += vsl_glob_test

vsl_glob_test_SOURCES = vsl_glob_test.c
vsl_glob_test_CFLAGS = @SAN_CFLAGS@
vsl_glob_test_LDADD = libvarnishapi.la @SAN_LDFLAGS@

dist_noinst_SCRIPTS = vsl_glob_test_coverage

TESTS = vsl_glob_test_coverage
