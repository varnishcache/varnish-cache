# $Id$

INCLUDES = -I$(top_srcdir)/include

lib_LTLIBRARIES = libvarnish.la

libvarnish_la_LDFLAGS = -version-info 1:0:0

libvarnish_la_SOURCES = \
	argv.c \
	assert.c \
	binary_heap.c \
	subproc.c \
	cli.c \
	cli_common.c \
	flopen.c \
	inxorcise.c \
	num.c \
	svn_version.c \
	time.c \
	tcp.c \
	vct.c \
	version.c \
	vev.c \
	vlu.c \
	vpf.c \
	vsb.c \
	vsha256.c \
	vss.c \
	vtmpfile.c

libvarnish_la_LIBADD = ${RT_LIBS} ${NET_LIBS} ${LIBM}

svn_version.c: FORCE
	echo '#include <libvarnish.h>' > svn_version.c
	echo 'const char* svn_version(void)' >> svn_version.c
	echo -n '{ const char* SVN_Version = "' >> svn_version.c
	svnversion -n $(top_srcdir)          >> svn_version.c
	echo '"; return SVN_Version; }'      >> svn_version.c
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
