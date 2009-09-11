#!/bin/sh

flexelint \
	flint.lnt \
	-I. \
	-I../../include \
	-I../.. \
	-DVARNISH_STATE_DIR=\"foo\" \
	*.c \
	../../lib/libvarnish/*.c \
	../../lib/libvcl/*.c
