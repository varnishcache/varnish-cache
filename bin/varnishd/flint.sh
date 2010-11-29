#!/bin/sh

flexelint \
	../flint.lnt \
	flint.lnt \
	-I. \
	-I../../include \
	-I../.. \
	-I/usr/local/include \
	-DVARNISH_STATE_DIR=\"foo\" \
	*.c \
	../../lib/libvarnish/*.c \
	../../lib/libvarnishcompat/execinfo.c \
	../../lib/libvcl/*.c \
	../../lib/libvmod_std/*.c \
	2>&1 | tee _.fl

if [ -f _.fl.old ] ; then
	diff -u _.fl.old _.fl
fi

mv _.fl _.fl.old
