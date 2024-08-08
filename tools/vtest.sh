#!/bin/sh
#
# Copyright (c) 2006-2016 Varnish Software AS
# All rights reserved.
#
# Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.

set -e

#######################################################################
# Parameters

export MAKEFLAGS="${MAKEFLAGS:--j2}"

# This temp directory must not be used by anything else.
# Do *NOT* set this to /tmp
export TMPDIR="${TMPDIR:-`pwd`/_vtest_tmp}"

# Message to be shown in result pages
# Max 10 char of [A-Za-z0-9/. _-]
MESSAGE="${MESSAGE:-}"

WAITPERIOD=60		# unit: Seconds
WAITMIN=1		# unit: WAITPERIOD
WAITMAX=60		# unit: WAITPERIOD

MAXRUNS="${MAXRUNS:-0}"

#######################################################################
# NB: No User Serviceable Parts Beyond This Point
#######################################################################

enable_gcov=false

: ${SSH_DST:="-p 203 vtest@varnish-cache.org"}

# make sure we use our own key
unset SSH_AUTH_SOCK

export REPORTDIR=`pwd`/_report
export VTEST_REPORT="${REPORTDIR}/_log"

#######################################################################
# Establish TMPDIR

mkdir -p "${TMPDIR}"
rm -rf "${TMPDIR:?}"/*

# Try to make varnish own TMPDIR, in case we run as root
chown varnish "${TMPDIR}" > /dev/null 2>&1 || true

#######################################################################
# Establish the SRCDIR we build/run/test

if ! (cd varnish-cache 2>/dev/null) ; then
	git clone \
		https://github.com/varnishcache/varnish-cache.git \
		varnish-cache
fi

export SRCDIR=`pwd`/varnish-cache

#######################################################################
# Submission of results

if [ ! -f vt_key.pub ] ; then
	ssh-keygen -t ed25519 -N "" -f vt_key
fi

pack () (
	cd "${REPORTDIR}"
	tar czf - _log \
	    `grep '^MANIFEST ' _log | sort -u | sed 's/^MANIFEST *//'` \
)

submit () (
	ssh \
		-T \
		-o StrictHostKeyChecking=no \
		-o PasswordAuthentication=no \
		-o NumberOfPasswordPrompts=0 \
		-o RequestTTY=no \
		-i vt_key \
		${SSH_DST} \
		true \
		< "${1}"
)

rm -f "${TMPDIR}"/_report.tgz
touch "${TMPDIR}"/_report.tgz

if ! submit "${TMPDIR}"/_report.tgz; then
	echo "Test submit failed"
	echo
	echo "You probably need to email this VTEST specific ssh-key"
	echo "to phk@varnish-cache.org"
	echo
	sed 's/^/  /' vt_key.pub
	echo
	exit 2
fi

#######################################################################

autogen () (
	set -e
	cd "${SRCDIR}"
	nice make distclean > /dev/null 2>&1 || true
	nice sh "${SRCDIR}"/autogen.des $AUTOGEN_FLAGS
)

makedistcheck () (
	set -e
	cd "${SRCDIR}"
	nice make vtest-clean
	nice make distcheck DISTCHECK_CONFIGURE_FLAGS=--with-persistent-storage
)

gcovtest () (
	set -x
	if [ `id -u` -eq 0 ] && su -m varnish -c 'true' ; then
		su -m varnish -c "make check" || exit 1
		cd bin/varnishtest
		./varnishtest -i tests/[ab]0000?.vtc tests/j*.vtc || exit 1
	else
		make check || exit 1
	fi
)

makegcov () (
	set -x
	cd "${SRCDIR}"

	make || exit 1

	if [ `id -u` -eq 0 ] ; then
		chown -R varnish . | true
	fi

	if gcovtest && make gcov_digest ; then
		retval=0
	else
		retval=1
	fi

	if [ `id -u` -eq 0 ] ; then
		chown -R root . || true
	fi
	exit ${retval}
)

failedtests () (
	set -e

	cd "${SRCDIR}"

	VTCDIR=bin/varnishtest/tests

	VERSION=`./configure --version | awk 'NR == 1 {print $NF}'`
	LOGDIR="varnish-$VERSION/_build/sub/bin/varnishtest/tests"

	# cope with older automake, remove the sub directory
	test ! -d $LOGDIR &&
	LOGDIR="varnish-$VERSION/_build/bin/varnishtest/tests"

	# gcov situation
	test ! -d $LOGDIR &&
	LOGDIR="bin/varnishtest/tests"

	find . -name '*.trs' -print | xargs grep -l ':test-result: FAIL' |
	while read trs
	do
		name=`basename "${trs}" .trs`
		vtc=`echo $trs | sed -E -e 's/trs$/vtc/' -e 's/.*_build\/(sub\/)?//'`
		logfile=`echo $trs | sed -e 's/trs$/log/'`
		log="${name}.log"
		if [ -f ${vtc} ] ; then
			rev=`git log -n 1 --pretty=format:%H "${vtc}"`
		else
			rev="?"
		fi
		cp "${logfile}" "${REPORTDIR}/_${log}"
		echo "VTCGITREV ${name} ${rev}"
		echo "MANIFEST _${log}"
	done
)

if $enable_gcov ; then
	#export CC=gcc6
	#export CC=clang80
	export GCOVPROG='llvm-cov gcov'
	export AUTOGEN_FLAGS='--enable-coverage --enable-stack-protector'
	export MAKEFLAGS=-j1
fi

orev=000
waitnext=0
waitcur=${WAITMIN}
i=0

while [ $MAXRUNS -eq 0 ] || [ $i -lt $MAXRUNS ]
do
	i=$((i + 1))

	(
		cd "${SRCDIR}"
	        chmod -R +w varnish-trunk > /dev/null 2>&1 || true
	        rm -rf varnish-trunk > /dev/null 2>&1 || true
	        git reset --hard > /dev/null 2>&1 || true
	        git clean -df > /dev/null 2>&1 || true
	        git pull > /dev/null 2>&1 || true
	)
	rev=`cd "${SRCDIR}" && git show -s --pretty=format:%H`
	if [ "x${rev}" != "x${orev}" ] ; then
		waitcur=${WAITMIN}
	elif [ "${waitnext}" -gt 0 ] ; then
		sleep ${WAITPERIOD}
		waitnext=`expr ${waitnext} - 1 || true`
		continue
	else
		waitcur=`expr ${waitcur} + 1`
		if [ ${waitcur} -gt ${WAITMAX} ] ; then
			waitcur=${WAITMAX}
		fi
	fi

	waitnext=${waitcur}

	orev=${rev}

	if ! [ -d "${SRCDIR}" ] && ! mkdir -p "${SRCDIR}" ; then
		echo >&2 "could not create SRCDIR ${SRCDIR}"
		exit 2
	fi

	rm -rf "${REPORTDIR}"
	mkdir "${REPORTDIR}"

	# NB:  Only change the report version number when the format/content
	# NB:  of the report changes.  Corresponding changes on the backend
	# NB:  will be required.  Coordinate with phk@.
	echo "VTEST 1.05" > "${VTEST_REPORT}"
	echo "DATE `date +%s`" >> "${VTEST_REPORT}"
	echo "BRANCH trunk" >> "${VTEST_REPORT}"
	echo "HOST `hostname`" >> "${VTEST_REPORT}"
	echo "UNAME `uname -a`" >> "${VTEST_REPORT}"
	echo "UGID `id`" >> "${VTEST_REPORT}"
	if [ -x /usr/bin/lsb_release ] ; then
		echo "LSB `lsb_release -d`" >> "${VTEST_REPORT}"
	else
		echo "LSB none" >> "${VTEST_REPORT}"
	fi
	echo "MESSAGE ${MESSAGE}" >> "${VTEST_REPORT}"
	echo "GITREV $rev" >> "${VTEST_REPORT}"

	find . -name '*.gc??' -print | xargs rm -f

	if ! autogen >> "${REPORTDIR}"/_autogen 2>&1 ; then
		echo "AUTOGEN BAD" >> "${VTEST_REPORT}"
		echo "MANIFEST _autogen" >> "${VTEST_REPORT}"
	else
		echo "AUTOGEN GOOD" >> "${VTEST_REPORT}"
		cp ${SRCDIR}/config.h "${REPORTDIR}"/_configh
		cp ${SRCDIR}/config.log "${REPORTDIR}"/_configlog
		echo "MANIFEST _configh" >> "${VTEST_REPORT}"
		echo "MANIFEST _configlog" >> "${VTEST_REPORT}"
		if $enable_gcov ; then
			if makegcov >> "${REPORTDIR}"/_makegcov 2>&1 ; then
				mv ${SRCDIR}/_gcov "${REPORTDIR}"/
				echo "MAKEGCOV GOOD" >> "${VTEST_REPORT}"
				echo "MANIFEST _gcov" >> "${VTEST_REPORT}"
				waitcur=${WAITMAX}
				waitnext=${WAITMAX}
			else
				echo "MAKEGCOV BAD" >> "${VTEST_REPORT}"
				echo "MANIFEST _makegcov" >> "${VTEST_REPORT}"
				failedtests >> "${VTEST_REPORT}"
			fi
		elif ! makedistcheck >> "${REPORTDIR}"/_makedistcheck 2>&1 ; then
			echo "MAKEDISTCHECK BAD" >> "${VTEST_REPORT}"
			echo "MANIFEST _autogen" >> "${VTEST_REPORT}"
			echo "MANIFEST _makedistcheck" >> "${VTEST_REPORT}"
			failedtests >> "${VTEST_REPORT}"
		else
			echo "MAKEDISTCHECK GOOD" >> "${VTEST_REPORT}"
			waitnext=${WAITMAX}
			waitcur=${WAITMAX}
		fi
	fi
	echo "VTEST END" >> "${VTEST_REPORT}"
	pack > "${TMPDIR}"/_report.tgz

	submit "${TMPDIR}"/_report.tgz || \
		sleep 300 || \
		submit "${TMPDIR}"/_report.tgz || \
		true
done
