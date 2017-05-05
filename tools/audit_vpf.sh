#!/bin/sh
#
# Script to compare vpf.c with FreeBSD's pidfile.c
#
# Run this on a up-to-date FreeBSD source tree

sed '
s/vpf_/pidfile_/g
s/VPF_/pidfile_/g
s/pidfile_fh/pidfh/g
s/pidfile_Write/pidfile_write/g
s/pidfile_Close/pidfile_close/g
s/pidfile_Remove/pidfile_remove/g
s/pidfile_Open/pidfile_open/g
s/	(void)/	/g
' lib/libvarnish/vpf.c |
    diff -ub /usr/src/lib/libutil/pidfile.c -

