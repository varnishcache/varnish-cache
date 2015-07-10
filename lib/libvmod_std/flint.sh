#!/bin/sh

flexelint \
	-u \
	../../flint.lnt \
	-I. \
	-I../.. \
	-I../../include \
	-I../../bin/varnishd \
	*.c
