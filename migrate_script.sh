#!/bin/sh

set -e

start_from_fresh() (
	echo "---------------------------------------- start_from_fresh"
	make -j 4 distclean > /dev/null 2>&1 || true
	rm -rf lib/libvtest_ext_vinyl
	rm -rf pretend_vtest
	git reset --hard
)


move_testcases() (
	echo "---------------------------------------- move_testcases"
	rm -rf tests

	# These tests recursively starts varnishtest
	git rm bin/varnishtest/tests/r02262.vtc
	git rm bin/varnishtest/tests/u00018.vtc

	bulk() (
		td=$1
		shift
		mkdir -p $td
		for fn in $*
		do
			bn=`basename $fn`
			git mv $fn $td/$bn
			sed -i '' '
				/^varnishtest/s//vtest/
				/varnishtest/s//vinyltest/
				/^varnish/s/varnish /vinyl /
				/^logexpect/s/logexpect /vsl_expect /
			' $td/$bn
		done
	)
	bulk tests/basic_functionality		bin/varnishtest/tests/b*
	bulk tests/complex_functionality	bin/varnishtest/tests/c*
	bulk tests/directors			bin/varnishtest/tests/d*
	bulk tests/edge_side_includes		bin/varnishtest/tests/e*
	bulk tests/security			bin/varnishtest/tests/f*
	bulk tests/gzip				bin/varnishtest/tests/g*
	bulk tests/haproxy			bin/varnishtest/tests/h*
	bulk tests/compliance			bin/varnishtest/tests/i*
	bulk tests/jailing			bin/varnishtest/tests/j*
	bulk tests/logging			bin/varnishtest/tests/l*
	bulk tests/infra_vmod			bin/varnishtest/tests/m*
	bulk tests/proxy_protocol		bin/varnishtest/tests/o*
	bulk tests/persistence			bin/varnishtest/tests/p*
	bulk tests/regressions			bin/varnishtest/tests/r*
	bulk tests/notably_slow_tests		bin/varnishtest/tests/s*
	bulk tests/http2			bin/varnishtest/tests/t02*
	bulk tests/utilities			bin/varnishtest/tests/u*
	bulk tests/VCL				bin/varnishtest/tests/v*
	bulk tests/extensions			bin/varnishtest/tests/x*

	bulk tests/vmod_blob			vmod/tests/blob*.vtc
	bulk tests/vmod_cookie			vmod/tests/cookie*.vtc
	bulk tests/vmod_directors		vmod/tests/directors*.vtc
	bulk tests/vmod_h2			vmod/tests/h2*.vtc
	bulk tests/vmod_proxy			vmod/tests/proxy*.vtc
	bulk tests/vmod_purge			vmod/tests/purge*.vtc
	bulk tests/vmod_std			vmod/tests/std*.vtc
	bulk tests/vmod_unix			vmod/tests/unix*.vtc
	bulk tests/vmod_vtc			vmod/tests/vtc*.vtc
)

adjust_testcases() (
	echo "---------------------------------------- adjust_testcases"

	sed -i '' '
1a\
\
# force load vtest_ext_vinyl to define vinyl_branch macro\
vinyl v1
s/pkg_branch/vinyl_branch/g
' tests/regressions/r03794.vtc

)

pretend_vtest_pkg() (
	echo "---------------------------------------- pretend_vtest_pkg"
	# Pretend we have the, not yet existing VTEST package installed
	VT2D=/home/phk/Varnish/VTest2
	# VT2D=bin/varnishtest/vtest2
	rm -rf pretend_vtest
	( cd ${VT2D} && make clean && make)
	mkdir -p pretend_vtest/bin pretend_vtest/include
	cp ${VT2D}/vtest pretend_vtest/bin/vtest
	cp ${VT2D}/src/vtest_api.h pretend_vtest/include
)

slam_new_stuff_in() (
	echo "---------------------------------------- slam_new_stuff_in"
	rm -rf lib/libvtest_ext_vinyl
	(cd Migration && find lib/libvtest_ext_vinyl -print | cpio -dumpv ..)
)

libvtest_ext_vinyl() (
	echo "---------------------------------------- libvtest_ext_vinyl"
	LVEV=lib/libvtest_ext_vinyl
	VT2=bin/varnishtest/vtest2
	VT2=/home/phk/Varnish/VTest2
        for fn in vtc_varnish.c vtc_vsm.c vtc_logexp.c
	do
		sed '
		/include/s/vtc.h/vtest_ext_vinyl.h/
		s/"varnish"/"vinyl"/
		s/"logexpect"/"vsl_expect"/
		' ${VT2}/src/${fn} > ${LVEV}/${fn}
	done

	sed -i '' '/vcc/a\
	libvtest_ext_vinyl \\
	' lib/Makefile.am

	sed -i '' '/lib.libvgz.Makefile/a\
    lib/libvtest_ext_vinyl/Makefile

	' configure.ac
)

run_autogen() (
	echo "---------------------------------------- run_autogen"
	env CPPFLAGS=-I`pwd`/pretend_vtest/include sh autogen.des
)

uncouple_varnishtest() (
	echo "---------------------------------------- uncouple_varnishtest"
	sed -i '' '/varnishtest/d' man/Makefile.am
	sed -i '' '
		/varnishtop.*/s//varnishtop/
		/varnishtest/d
	' bin/Makefile.am

	sed -i '' '
	/^# Stupid automake/{
N
N
N
N
N
N
N
d
}
	/bin\/varnishtest\/Makefile/d
	' configure.ac

	sed -i '' '
	/^VTC_LOG_COMPILER/s/=.*/= $(srcdir)\/vtest_script.sh/
	' vtc.am
)

couple_in_tests() (
	echo "---------------------------------------- couple_in_tests"

	sed -i '' '
	/SUBDIRS =/s/$/ tests/
	' Makefile.am

	echo '
SUBDIRS = \
	basic_functionality

FOOSUBDIRS = \
	vmod_blob \
	complex_functionality \
	compliance \
	edge_side_includes \
	extensions \
	gzip \
	http2 \
	haproxy \
	jailing \
	logging \
	notably_slow_tests \
	persistence \
	proxy_protocol \
	regressions \
	security \
	infra_vmod \
	VCL \
	utilities \
	vmod_blob \
	vmod_cookie \
	vmod_directors \
	vmod_h2 \
	vmod_proxy \
	vmod_purge \
	vmod_std \
	vmod_unix \
	vmod_vtc
	' > tests/Makefile.am

	echo '
VTC_LOG_DRIVER = $(top_srcdir)/build-aux/vtest_driver.sh --top-srcdir ${top_srcdir} --top-builddir ${top_builddir}
TEST_EXTENSIONS = .vtc
 
TESTS   = \
	b00000.vtc \
	b00001.vtc

	' > tests/basic_functionality/Makefile.am

	sed -i '' '/vmod\/Makefile/a\
    tests\/Makefile\
    tests\/basic_functionality\/Makefile
	' configure.ac
)

#######################################################################

if ! grep -q 'More platforms are tested via vtest_' README.rst ; then
	echo "You are in the wrong directory for this..."
	exit 2
fi

start_from_fresh

move_testcases

adjust_testcases

pretend_vtest_pkg

slam_new_stuff_in

libvtest_ext_vinyl

uncouple_varnishtest

couple_in_tests

run_autogen

make -j 4

make check

env LD_LIBRARY_PATH=`pwd`/lib/libvtest_ext_vinyl/.libs \
	pretend_vtest/bin/vtest -j 4 -i -k tests/*/*.vtc 2>&1 |
	tee _
