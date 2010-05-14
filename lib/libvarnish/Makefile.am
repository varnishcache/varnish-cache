# $Id$

INCLUDES = -I$(top_srcdir)/include @PCRE_CFLAGS@

lib_LTLIBRARIES = libvarnish.la

libvarnish_la_LDFLAGS = -version-info 1:0:0

libvarnish_la_SOURCES = \
	argv.c \
	assert.c \
	binary_heap.c \
	subproc.c \
	cli_auth.c \
	cli_common.c \
	cli_serve.c \
	flopen.c \
	num.c \
	svn_version.c \
	time.c \
	tcp.c \
	vct.c \
	version.c \
	vev.c \
	vin.c \
	vlu.c \
	vpf.c \
	vre.c \
	vsb.c \
	vsha256.c \
	vss.c \
	vtmpfile.c

libvarnish_la_CFLAGS = -DVARNISH_STATE_DIR='"${VARNISH_STATE_DIR}"'
libvarnish_la_LIBADD = ${RT_LIBS} ${NET_LIBS} ${LIBM} @PCRE_LIBS@

DISTCLEANFILES = svn_version.c
svn_version.c: FORCE
	V="$$(sh -c "git log -n 1 --pretty=format:%h" 2>/dev/null || LANG=C svnversion -n $(top_srcdir))" \
	H="$$(head -n 1 svn_version.c 2>/dev/null || true)"; \
	[ "$$V" = "exported" ] && [ -e svn_version.c ] && exit 0 ; \
	if [ "/* $$V */" != "$$H" ]; then \
		( \
		  echo "/* $$V */" ;\
		  echo "#include <libvarnish.h>" ;\
		  echo "const char* svn_version(void)" ;\
		  echo "{" ;\
		  echo "	const char* SVN_Version = \"$$V\";" ;\
		  echo "	return SVN_Version;" ;\
		  echo "}" ;\
		) > svn_version.c ; \
	fi
FORCE:

if ENABLE_TESTS
TESTS = num_c_test

noinst_PROGRAMS = ${TESTS}

num_c_test_SOURCES = num.c
num_c_test_CFLAGS = -DNUM_C_TEST -include config.h
num_c_test_LDADD = ${LIBM}

test: ${TESTS}
	@for test in ${TESTS} ; do ./$${test} ; done
endif
