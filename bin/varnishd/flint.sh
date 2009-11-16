#!/bin/sh

flexelint \
	flint.lnt \
	-I. \
	-I../../include \
	-I../.. \
	-I/usr/local/include \
	-DVARNISH_STATE_DIR=\"foo\" \
	*.c \
	../../lib/libvarnish/*.c \
	../../lib/libvcl/*.c
