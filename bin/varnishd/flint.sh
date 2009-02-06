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
	../../lib/libvcl/*.c 
