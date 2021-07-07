#!/bin/sh

if [ "x$1" = "x-ok" -a -f _.fl ] ; then
	echo "Saved as reference"
	mv _.fl _.fl.old
	exit 0
fi


d=$(dirname $0)
if [ $d = ../../tools ] ; then
    l=2
    IARG="-I. -I../../include -I../.."
else
    l=1
    IARG="-I. -I../include -I.."
fi

IARG="${IARG} -I/usr/local/include"

# Flexelint do not grok the macro-stacking in pcre2.h
# Solve it by running it through CPP first.

echo '
#include "config.h"
#include <pcre2.h>
' | cc $IARG -E - |
    sed '/^# [0-9]/d' > pcre2.h

flexelint \
	-D__FLEXELINT__ \
	$(if [ $l -eq 2 ] ; then echo ../../flint.lnt ; fi) \
	../flint.lnt \
	flint.lnt \
	-zero \
	-I. \
        ${IARG} \
	$FLOPS \
	2>&1 | tee _.fl

rm -f pcre2.h

if [ -f _.fl.old ] ; then
	diff -u _.fl.old _.fl
fi
