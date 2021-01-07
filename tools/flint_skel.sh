#!/bin/sh

if [ "x$1" = "x-ok" -a -f _.fl ] ; then
	echo "Saved as reference"
	mv _.fl _.fl.old
	exit 0
fi

d=$(dirname $0)
l=1
if [ $d = ../../tools ] ; then
    l=2
fi

flexelint \
	-D__FLEXELINT__ \
	$(if [ $l -eq 2 ] ; then echo ../../flint.lnt ; fi) \
	../flint.lnt \
	flint.lnt \
	-zero \
	-I. \
	$(if [ $l -eq 2 ] ; then
	      echo -I../../include
	      echo -I../..
	  else
	      echo -I../include
	      echo -I..
	  fi
	) \
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
