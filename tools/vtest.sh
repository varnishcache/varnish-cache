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

export TMPDIR=`pwd`/tmp
mkdir -p tmp

# Message to be shown in result pages
# Max 10 char of [A-Za-z0-9/. _-]
MESSAGE="${MESSAGE:-}"

WAITPERIOD=60		# unit: Seconds

WAITGOOD=60		# unit: WAITPERIOD
WAITBAD=1		# unit: WAITPERIOD
MAXRUNS="${MAXRUNS:-0}"

SSH_DST="-p 203 vtest@varnish-cache.org"

export SRCDIR=`pwd`/varnish-cache
export BUILDDIR=${BUILDDIR:-${SRCDIR}}

#######################################################################

if ! (cd varnish-cache 2>/dev/null) ; then
	git clone \
		https://github.com/varnishcache/varnish-cache.git \
		varnish-cache
fi

if [ ! -f vt_key.pub ] ; then
	ssh-keygen -t ed25519 -N "" -f vt_key
fi

autogen () (
	set -e
	cd "${BUILDDIR}"
	nice make distclean > /dev/null 2>&1 || true
	nice sh "${SRCDIR}"/autogen.des
)

makedistcheck () (
	set -e
	cd "${BUILDDIR}"
	nice make distcheck
)

failedtests () (
	for t in `grep '^FAIL: tests/' ${1} | sort -u | sed 's/.* //'`
	do
		printf 'VTCGITREV %s ' "${t}"
		(
			cd varnish-cache/bin/varnishtest/
			git log -n 1 ${t} | head -1
		)
		b=`basename ${t} .vtc`
		for i in `find "${BUILDDIR}" -name ${b}.log -print`
		do
			if [ -f ${i} ] ; then
				mv ${i} "_report/_${b}.log"
				echo "MANIFEST _${b}.log" >> ${LOG}
			fi
		done
	done
)

pack () (
	cd _report
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

rm -f _report.tgz
touch _report.tgz
if ! submit _report.tgz; then
	echo "Test submit failed"
	echo
	echo "You probably need to email this VTEST specific ssh-key"
	echo "to phk@varnish-cache.org"
	echo
	sed 's/^/  /' vt_key.pub
	echo
	exit 2
fi

orev=000
waitnext=${WAITBAD}
i=0

while [ $MAXRUNS -eq 0 ] || [ $i -lt $MAXRUNS ]
do
	i=$((i + 1))

	(cd varnish-cache && git pull > /dev/null 2>&1 || true)
	rev=`cd varnish-cache && git show -s --pretty=format:%H`
	if [ "${waitnext}" -gt 0 -a "x${rev}" = "x${orev}" ] ; then
		sleep ${WAITPERIOD}
		waitnext=`expr ${waitnext} - 1 || true`
		continue
	fi
	waitnext=${WAITBAD}
	orev=${rev}

	if ! [ -d "${BUILDDIR}" ] && ! mkdir -p "${BUILDDIR}" ; then
		echo >&2 "could not create BUILDDIR ${BUILDDIR}"
		exit 2
	fi

	rm -rf _report
	mkdir _report
	export LOG=_report/_log

	echo "VTEST 1.02" > ${LOG}
	echo "DATE `date +%s`" >> ${LOG}
	echo "BRANCH trunk" >> ${LOG}
	echo "HOST `hostname`" >> ${LOG}
	echo "UNAME `uname -a`" >> ${LOG}
	if [ -x /usr/bin/lsb_release ] ; then
		echo "LSB `lsb_release -d`" >> ${LOG}
	else
		echo "LSB none" >> ${LOG}
	fi
	echo "MESSAGE ${MESSAGE}" >> ${LOG}
	echo "GITREV $rev" >> ${LOG}
	if ! autogen >> _report/_autogen 2>&1 ; then
		echo "AUTOGEN BAD" >> ${LOG}
		echo "MANIFEST _autogen" >> ${LOG}
	else
		echo "AUTOGEN GOOD" >> ${LOG}
		if ! makedistcheck >> _report/_makedistcheck 2>&1 ; then
			echo "MAKEDISTCHECK BAD" >> ${LOG}
			echo "MANIFEST _autogen" >> ${LOG}
			echo "MANIFEST _makedistcheck" >> ${LOG}
			failedtests _report/_makedistcheck >> ${LOG}
		else
			echo "MAKEDISTCHECK GOOD" >> ${LOG}
			waitnext=${WAITGOOD}
		fi
	fi
	echo "VTEST END" >> ${LOG}
	pack > _report.tgz
	submit _report.tgz
done
