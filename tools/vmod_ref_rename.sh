#!/bin/bash

# trivial sed-based tool to rename RST references in vmod .vcc files
# to the new scheme as of commit
# 904ceabf07c294983efcebfe1d614c2d2fd5cdda
#
# only tested with GNU sed
#
# to be called from a vmod git repository
#
# Author: Nils Goroll <nils.goroll@uplex.de>
#
# This file is in the public domain

typeset -ra files=($(git ls-files | grep -E '\.vcc$'))

if [[ ${#files[@]} -eq 0 ]] ; then
    echo >&2 'No vcc files found'
    exit 0
fi

if [[ -n $(git status -s) ]] ; then
    echo >&2 'clean up your tree first'
    git status
    exit 1
fi

set -eu

sed -e 's#`vmod_\([^.]*\)\.\([^.`]*\)`_#`\1.\2()`_#g' \
    -e 's#`vmod_\([^.]*\)\.\([^.`]*\).\([^.`]*\)`_#`x\2.\3()`_#g' \
    -i "${files[@]}"

if [[ -z $(git status -s) ]] ; then
    echo >&2 'no change'
    exit 0
fi

git commit -m 'rename vmod RST references (vmod_ref_rename.sh)' \
    "${files[@]}"

echo DONE - review and test the previous commit
