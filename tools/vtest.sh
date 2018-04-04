#!/bin/sh
#
# Copyright (c) 2006-2016 Varnish Software AS
# All rights reserved.
#
# Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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

# This tempdirectory must not be used by anything else.
# Do *NOT* set this to /tmp
export TMPDIR=`pwd`/_vtest_tmp

# Message to be shown in result pages
# Max 10 char of [A-Za-z0-9/. _-]
MESSAGE="${MESSAGE:-}"

WAITPERIOD=60		# unit: Seconds

WAITGOOD=60		# unit: WAITPERIOD
WAITBAD=1		# unit: WAITPERIOD
MAXRUNS="${MAXRUNS:-0}"

#######################################################################
# NB: No User Serviceable Parts Beyond This Point
#######################################################################

enable_gcov=false

SSH_DST="-p 203 vtest@varnish-cache.org"

export REPORTDIR=`pwd`/_report
export VTEST_REPORT="${REPORTDIR}/_log"

#######################################################################
# Establish TMPDIR

mkdir -p ${TMPDIR}
rm -rf ${TMPDIR}/*

# Try to make varnish own TMPDIR, in case we run as root
chown varnish ${TMPDIR} > /dev/null 2>&1 || true

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
	cd ${REPORTDIR}
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
		< ${1}
)

rm -f ${TMPDIR}/_report.tgz
touch ${TMPDIR}/_report.tgz

if ! submit ${TMPDIR}/_report.tgz; then
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
	nice sh "${SRCDIR}"/autogen.des
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

	grep -l ':test-result: FAIL' "$LOGDIR"/*.trs |
	while read trs
	do
		name=`basename $trs .trs`
		vtc="${name}.vtc"
		log="${name}.log"
		rev=`git log -n 1 --pretty=format:%H "${VTCDIR}/${vtc}"`
		cp "${LOGDIR}/${log}" "${REPORTDIR}/_${log}"
		echo "VTCGITREV ${name} ${rev}"
		echo "MANIFEST _${log}"
	done
)

if $enable_gcov ; then
	export CC=gcc6
	export CFLAGS="-fprofile-arcs -ftest-coverage -fstack-protector -DDONT_DLCLOSE_VMODS"
	export MAKEFLAGS=-j1
fi

orev=000
waitnext=${WAITBAD}
i=0

while [ $MAXRUNS -eq 0 ] || [ $i -lt $MAXRUNS ]
do
	i=$((i + 1))

	(cd "${SRCDIR}" && git pull > /dev/null 2>&1 || true)
	rev=`cd "${SRCDIR}" && git show -s --pretty=format:%H`
	if [ "${waitnext}" -gt 0 -a "x${rev}" = "x${orev}" ] ; then
		sleep ${WAITPERIOD}
		waitnext=`expr ${waitnext} - 1 || true`
		continue
	fi
	waitnext=${WAITBAD}
	orev=${rev}

	if ! [ -d "${SRCDIR}" ] && ! mkdir -p "${SRCDIR}" ; then
		echo >&2 "could not create SRCDIR ${SRCDIR}"
		exit 2
	fi

	rm -rf "${REPORTDIR}"
	mkdir "${REPORTDIR}"

	echo "VTEST 1.04" > ${VTEST_REPORT}
	echo "DATE `date +%s`" >> ${VTEST_REPORT}
	echo "BRANCH trunk" >> ${VTEST_REPORT}
	echo "HOST `hostname`" >> ${VTEST_REPORT}
	echo "UNAME `uname -a`" >> ${VTEST_REPORT}
	echo "UGID `id`" >> ${VTEST_REPORT}
	if [ -x /usr/bin/lsb_release ] ; then
		echo "LSB `lsb_release -d`" >> ${VTEST_REPORT}
	else
		echo "LSB none" >> ${VTEST_REPORT}
	fi
	echo "MESSAGE ${MESSAGE}" >> ${VTEST_REPORT}
	echo "GITREV $rev" >> ${VTEST_REPORT}

	find . -name '*.gc??' -print | xargs rm -f

	if ! autogen >> ${REPORTDIR}/_autogen 2>&1 ; then
		echo "AUTOGEN BAD" >> ${VTEST_REPORT}
		echo "MANIFEST _autogen" >> ${VTEST_REPORT}
	else
		echo "AUTOGEN GOOD" >> ${VTEST_REPORT}
		if $enable_gcov ; then
			if makegcov >> ${REPORTDIR}/_makegcov 2>&1 ; then
				mv ${SRCDIR}/_gcov ${REPORTDIR}/
				echo "MAKEGCOV GOOD" >> ${VTEST_REPORT}
				echo "MANIFEST _gcov" >> ${VTEST_REPORT}
				waitnext=${WAITGOOD}
			else
				echo "MAKEGCOV BAD" >> ${VTEST_REPORT}
				echo "MANIFEST _makegcov" >> ${VTEST_REPORT}
				failedtests >> ${VTEST_REPORT}
			fi
		elif ! makedistcheck >> ${REPORTDIR}/_makedistcheck 2>&1 ; then
			echo "MAKEDISTCHECK BAD" >> ${VTEST_REPORT}
			echo "MANIFEST _autogen" >> ${VTEST_REPORT}
			echo "MANIFEST _makedistcheck" >> ${VTEST_REPORT}
			failedtests >> ${VTEST_REPORT}
		else
			echo "MAKEDISTCHECK GOOD" >> ${VTEST_REPORT}
			waitnext=${WAITGOOD}
		fi
	fi
	echo "VTEST END" >> ${VTEST_REPORT}
	pack > ${TMPDIR}/_report.tgz

	submit ${TMPDIR}/_report.tgz || \
		sleep 300 || \
		submit ${TMPDIR}/_report.tgz || \
		true
done
