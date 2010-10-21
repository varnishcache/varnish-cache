#!/bin/sh

flexelint \
	-DTOP_BUILDDIR='"foo"' \
	-I/usr/include \
	-I. \
	-I../../include \
	-I../.. \
	../flint.lnt \
	flint.lnt \
	*.c 
