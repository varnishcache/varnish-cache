#!/bin/sh
#
# Copyright (c) 2021 Varnish Software AS
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
#
# Check that all magic numbers are declared as:
#
# - 0x00112233	(most magic numbers)
# - 0x00	(where size matters)

set -u

ROOT=$(git rev-parse --show-cdup 2>/dev/null) || exit 77
test -z "$ROOT" || exit 77

git grep -h '^#define \w*_MAGIC' |
sed 's/\\t/\t/g' |
awk '{print $3}' |
tr '[:upper:]' '[:lower:]' |
sort |
uniq -c |
sort |
awk '$1 != 1 || $2 !~ /^0x[0-9a-f]*$/ || length($2) !~ /^(4|10)$/' |
while read -r COUNT MAGIC
do
	if [ $COUNT -eq 1 ]
	then
		echo "Invalid magic number:"
	else
		echo "Duplicate magic number:"
	fi
	git grep -ih '^#define \w*_MAGIC\s*'$MAGIC
	echo
	false # propagate non-zero exit status
done
