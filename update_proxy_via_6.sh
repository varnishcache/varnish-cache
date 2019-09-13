#!/bin/bash

cp -p $0 /tmp/save.$$

set -eux
git reset --hard master
git merge 'VRT_DirectorResolve' 'VRT_Format_Proxy'
commits=(
    38ca633d6b8d085a9a5d73c44c22be76350e7d1f	# Add VTP_Clone() to get the same endpoint with
    aa577c6e8e06dfa2b3eedf5d5bd0f299151587c9	# VBE: Add support for a per-connection preamble
    a2f6ccd2f07bf6ef27d20aa63da227c76da97ff1	# add an ipv6 bogo ip by the name bogo_ip6
    c4d10f350d29d4c48cc035ce1c804338cb56d9b9	# Basic "via" backends support
    8aab43f30a8e0f54d3cc17ac229e34ee0a887efc	# via backends in VCL
    d912e8131dbbcb96465db4b422f296594494b5e2	# clarify that sa&bogo can not be undefined or NULL
    7cad23af2ce52872b8b7c66e4158e308ee6566c0	# Add the .authority field to backend definitions.
)
for c in "${commits[@]}" ; do
    if ! git cherry-pick "${c}" ; then
	echo SHELL to resolve conflict
	bash
    fi
done

cp -p /tmp/save.$$ $0
git add $0
git commit -m 'add the update script'
