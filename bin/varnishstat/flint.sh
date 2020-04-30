#!/bin/sh

FLOPS='
	varnishstat.c
	varnishstat_curses.c
	varnishstat_curses_help.c

	../../lib/libvarnishapi/flint.lnt
	../../lib/libvarnishapi/*.c
'

. ../../tools/flint_skel.sh
