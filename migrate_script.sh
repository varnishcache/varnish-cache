#!/bin/sh

set -e

start_from_fresh() (
	echo "---------------------------------------- start_from_fresh"
	make -j 4 distclean > /dev/null 2>&1 || true
	git reset --hard
)


move_testcases() (
	echo "---------------------------------------- move_testcases"
	rm -rf tests

	bulk() (
		td=$1
		shift
		mkdir -p $td
		for fn in $*
		do
			bn=`basename $fn`
			git mv $fn $td/$bn
			sed -i '' '
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
	bulk tests/vmod_support			bin/varnishtest/tests/m*
	bulk tests/proxy_protocol		bin/varnishtest/tests/o*
	bulk tests/persistence			bin/varnishtest/tests/p*
	bulk tests/regressions			bin/varnishtest/tests/r*
	bulk tests/notably_slow_tests		bin/varnishtest/tests/s*
	bulk tests/http2			bin/varnishtest/tests/t02*
	bulk tests/utilities			bin/varnishtest/tests/u*
	bulk tests/VCL				bin/varnishtest/tests/v*
	bulk tests/extensions			bin/varnishtest/tests/x*
)

pretend_vtest_pkg() (
	echo "---------------------------------------- pretend_vtest_pkg"
	# Pretend we have the, not yet existing VTEST package installed
	# VT2D=/home/phk/Varnish/VTest2
	VT2D=bin/varnishtest/vtest2
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

#######################################################################

if ! grep -q 'More platforms are tested via vtest_' README.rst ; then
	echo "You are in the wrong directory for this..."
	exit 2
fi

start_from_fresh

move_testcases

pretend_vtest_pkg

slam_new_stuff_in

libvtest_ext_vinyl

run_autogen

#(cd lib/libvarnish && make)
#(cd lib/libvarnishapi && make)

make -j 4
#(cd lib/libvtest_ext_vinyl && make -k)

env LD_LIBRARY_PATH=`pwd`/lib/libvtest_ext_vinyl/.libs \
	pretend_vtest/bin/vtest -i -k tests/*/*.vtc 2>&1 |
	tee _
