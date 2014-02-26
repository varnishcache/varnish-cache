#

AM_LDFLAGS  = $(AM_LT_LDFLAGS)

AM_CPPFLAGS = -I$(top_srcdir)/include @PCRE_CFLAGS@

lib_LTLIBRARIES = libvarnishapi.la

libvarnishapi_la_LDFLAGS = $(AM_LDFLAGS) -version-info 1:0:0

libvarnishapi_la_SOURCES = \
	vsm_api.h \
	vsl_api.h \
	vxp.h \
	vxp_tokens.h \
	\
	../libvarnish/vas.c \
	../libvarnish/vav.c \
	../../include/vcs_version.h \
	../libvarnish/version.c \
	../libvarnish/cli_common.c \
	../libvarnish/cli_auth.c \
	../libvarnish/vin.c \
	../libvarnish/vmb.c \
	../libvarnish/vre.c \
	../libvarnish/vsb.c \
	../libvarnish/vtim.c \
	../libvarnish/vsha256.c \
	vsm.c \
	vsl_arg.c \
	vsl_cursor.c \
	vsl_dispatch.c \
	vsl_query.c \
	vsl.c \
	vsc.c \
	vxp.c \
	vxp_parse.c \
	vxp_lexer.c \
	vxp_fixed_token.c \
	libvarnishapi.map

libvarnishapi_la_CFLAGS = \
	-DVARNISH_STATE_DIR='"${VARNISH_STATE_DIR}"'

libvarnishapi_la_LIBADD = @PCRE_LIBS@ @RT_LIBS@

if HAVE_LD_VERSION_SCRIPT
libvarnishapi_la_LDFLAGS += -Wl,--version-script=$(srcdir)/libvarnishapi.map
else
libvarnishapi_la_LDFLAGS += -export-symbols-regex '^V'
endif

EXTRA_DIST = \
	generate.py \
	vsl-tags.rst

BUILT_SOURCES = \
	vxp_fixed_token.c \
	vxp_tokens.h \
	vsl-tags.rst

CLEANFILES = \
	$(builddir)/vxp_fixed_token.c \
	$(builddir)/vxp_tokens.h

MAINTAINERCLEANFILES = \
	vsl-tags.rst

noinst_PROGRAMS = vsl2rst

vsl2rst_SOURCES = \
	vsl2rst.c \
	$(top_srcdir)/include/tbl/vsl_tags.h \
	$(top_srcdir)/include/tbl/vsl_tags_http.h

vsl-tags.rst: vsl2rst
	./vsl2rst > $@

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
