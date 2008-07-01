#!/bin/sh
#
# Run flexelint on the VCL output

if [ "x$1" = "x" ] ; then
	./varnishd -C -b localhost > /tmp/_.c
elif [ -f $1 ] ; then
	./varnishd -C -f $1 > /tmp/_.c
else
	echo "usage!" 1>&2
fi

flexelint vclflint.lnt /tmp/_.c 
