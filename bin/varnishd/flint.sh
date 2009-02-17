#!/bin/sh

T=/tmp/_$$
flexelint \
	-I/usr/include \
	-I. \
	-I../../include \
	-I../.. \
	-DVARNISH_STATE_DIR=\"foo\" \
	flint.lnt \
	*.c \
	../../lib/libvarnish/*.c \
	../../lib/libvcl/*.c \
	> $T
if [ -f _flint.old ] ; then
	diff -u _flint.old $T
	mv $T _flint.old
fi
cat $T
rm $T
