#!/bin/sh

flexelint \
	-I/usr/include \
	-I. \
	-I../../include \
	-I../.. \
	flint.lnt \
	*.c 
