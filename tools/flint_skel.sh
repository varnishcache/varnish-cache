#!/bin/sh

if [ "x$1" = "x-ok" -a -f _.fl ] ; then
	echo "Saved as reference"
	mv _.fl _.fl.old
	exit 0
fi

flexelint \
	../../flint.lnt \
	../flint.lnt \
	flint.lnt \
	-zero \
	-I. \
	-I../../include \
	-I../.. \
	-I/usr/local/include \
	$FLOPS \
	2>&1 | tee _.fl

if [ -f _.fl.old ] ; then
	diff -u _.fl.old _.fl
fi

if [ "x$1" = "x-ok" ] ; then
	echo "Saved as reference"
	mv _.fl _.fl.old
fi
