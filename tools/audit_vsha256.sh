#!/bin/sh
#
# Script to compare vsha256.c with FreeBSD's sha256c.c
#
# Run this on a up-to-date FreeBSD source tree

sed '
s/vbe32/be32/g
' lib/libvarnish/vsha256.c |
    diff -ub /usr/src/sys/crypto/sha2/sha256c.c -

