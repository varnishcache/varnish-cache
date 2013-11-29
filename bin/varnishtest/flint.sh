#!/bin/sh

flexelint \
	-DTOP_BUILDDIR='"foo"' \
	-I/usr/include \
	-I. \
	-I../../include \
	-I../../lib/libvgz \
	-I../.. \
	../../flint.lnt \
	../flint.lnt \
	flint.lnt \
	*.c \
	../../lib/libvarnishapi/*.c
