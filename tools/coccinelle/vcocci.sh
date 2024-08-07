#!/bin/sh
#
# Copyright (c) 2022 Varnish Software AS
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

readonly SCRIPT=$(basename $0)
readonly TMP=$(mktemp -d)
trap 'rm -rf $TMP' EXIT

usage() {
	ERR=${1:-1}
	test $ERR == 0 ||
	exec >&2

	cat <<-EOF
	Usage: $SCRIPT <command> [...]
	       $SCRIPT help

	Operate Coccinelle semantic patches.

	Available commands:

	    apply <file>    Apply a patch to the source tree
	    parse <file>    Parse and expand a patch
	    mkiso           Generate a varnish.iso file
	    help            Show this help and exit

	This script operates directly on the Varnish Cache git repository.
	EOF
	exit $ERR
}

errorf() {
	printf >&2 'Error: '
	printf >&2 "$@"
	printf >&2 '\n'
}

exec_spatch() {
	exec spatch \
		--macro-file "$SRCDIR/tools/coccinelle/vdef.h" \
		-I "$SRCDIR/include/" \
		-I "$SRCDIR/bin/varnishd/" \
		"$@"
}

cmd_apply() {
	if test $# != 1
	then
		errorf 'invalid arguments'
		usage
	fi
	exec_spatch --in-place --dir "$SRCDIR" --sp-file "$1"
}

cmd_parse() {
	if test $# != 1
	then
		errorf 'invalid arguments'
		usage
	fi
	exec_spatch --parse-cocci --sp-file "$1"
}

cmd_mkiso() {
	errorf 'not implemented'
	exit 1
}

git rev-parse --show-toplevel >/dev/null ||
usage

if test $# = 0
then
	errorf 'missing command'
	usage
fi

readonly SRCDIR=$(git rev-parse --show-toplevel)

CMD=$1
shift

case $CMD in
	apply)
		cmd_apply "$@"
		;;
	parse)
		cmd_parse "$@"
		;;
	mkiso)
		cmd_mkiso "$@"
		;;
	help)
		usage 0
		;;
	*)
		errorf "unknown command '%s'" "$CMD"
		usage
		;;
esac
