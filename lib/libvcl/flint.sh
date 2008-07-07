#!/bin/sh

flexelint \
	-I/usr/include \
	-I. \
	-I../.. \
	-I../../include \
	-I../../contrib/libevent \
	flint.lnt \
	*.c
