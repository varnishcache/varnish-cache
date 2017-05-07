#!/bin/sh
#
# Script to compare vfl.c with FreeBSD's flopen.c
#
# Run this on a up-to-date FreeBSD source tree

sed '
s/VFL_Open/flopen/
' lib/libvarnish/vfl.c |
    diff -u /usr/src/lib/libutil/flopen.c -

