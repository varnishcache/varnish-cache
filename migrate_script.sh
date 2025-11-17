#!/bin/sh

set -e

start_from_fresh() (
	echo "---------------------------------------- start_from_fresh"
	make -j 4 distclean > /dev/null 2>&1 || true
	rm -rf lib/libvtest_ext_vinyl
	rm -rf pretend_vtest
	rm -rf tests
	git reset --hard
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

python3 -u migrate.py

pretend_vtest_pkg

run_autogen

make -j 4

make -j 8 check
