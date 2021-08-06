#!/bin/sh
#
# Copyright (c) 2019, 2021 Varnish Software AS
# All rights reserved.
#
# Author: Dridi Boukelmoune <dridi.boukelmoune@gmail.com>
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
set -u

SCRIPT=$0

usage() {
	test $# -eq 1 &&
	printf 'Error: %s.\n\n' "$1"

	cat <<-EOF
	Usage: $SCRIPT [-b <rev>] [-g <rev>] [-i] [-j <jobs>] [<file>]
	       $SCRIPT -h

	Automatically look for the change that introduced a regression with
	the help of a test case.

	Available options:
	-h         : show this help and exit
	-b <rev>   : bad revision (defaults to HEAD)
	-g <rev>   : good revision (defaults to latest tag before bad)
	-i         : inverted mode, look for a bug fix instead
	-j <jobs>  : number of jobs for make invocations (defaults to 8)

	When <file> is empty or missing, bisect.vtc is expected to be found
	at the root of the git repository. The current source tree is used
	and VPATH setups are not supported.

	The -i option inverses the bisection behavior: the test case is now
	expected to fail on the good revision and pass on the bad revision.
	This is useful to track a bug that was fixed without being noticed.

	This script is expected to run from the root of the git repository
	as well.
	EOF
	exit $#
}

build() {
	make -s -j"$MAKE_JOBS" all || {
		./autogen.des
		make -s -j"$MAKE_JOBS" all
	}
}

run() {
	if build
	then
		# ignore unfortunate build-time modifications of srcdir
		git checkout -- .
	else
		git checkout -- .
		exit 125
	fi

	if [ -n "$INVERSE" ]
	then
		! bin/varnishtest/varnishtest -i "$VTC_FILE"
	else
		bin/varnishtest/varnishtest -i "$VTC_FILE"
	fi
	exit $?
}

BISECT_GOOD=
BISECT_BAD=
MAKE_JOBS=
INVERSE=
RUN_MODE=false
VTC_FILE=

while getopts b:g:hij:r OPT
do
	case $OPT in
	b) BISECT_BAD=$OPTARG ;;
	g) BISECT_GOOD=$OPTARG ;;
	h) usage ;;
	i) INVERSE=-i ;;
	j) MAKE_JOBS=$OPTARG ;;
	r) RUN_MODE=true ;; # -r usage is strictly internal
	*) usage "wrong usage" >&2 ;;
	esac
done

shift $((OPTIND - 1))

test $# -gt 1 && usage "too many arguments" >&2
test $# -eq 1 && VTC_FILE=$1

BISECT_BAD=${BISECT_BAD:-HEAD}
MAKE_JOBS=${MAKE_JOBS:-8}
VTC_FILE=${VTC_FILE:-bisect.vtc}

[ -n "$BISECT_GOOD" ] || BISECT_GOOD=$(git describe --abbrev=0 "$BISECT_BAD")

# run mode short circuit
"$RUN_MODE" && run

# bisect mode
ROOT_DIR=$(git rev-parse --show-toplevel)

readonly TMP_DIR=$(mktemp -d)

trap 'rm -rf $TMP_DIR' EXIT

cp "$ROOT_DIR"/tools/vtc-bisect.sh "$TMP_DIR"
cp "$VTC_FILE" "$TMP_DIR"/bisect.vtc

cd "$ROOT_DIR"

git bisect start
git bisect good "$BISECT_GOOD"
git bisect bad "$BISECT_BAD"
git bisect run "$TMP_DIR"/vtc-bisect.sh -r -j "$MAKE_JOBS" $INVERSE \
	"$TMP_DIR"/bisect.vtc
git bisect reset
