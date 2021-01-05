#!/bin/sh
#
# Run flexelint on the VCL output
LIBS="-p vmod_path=/home/phk/Varnish/trunk/varnish-cache/vmod/.libs"

if [ "x$1" = "x" ] ; then
	./varnishd $LIBS -C -b localhost > /tmp/_.c
elif [ -f $1 ] ; then
	./varnishd $LIBS -C -f $1 > /tmp/_.c
else
	echo "usage!" 1>&2
fi

flexelint vclflint.lnt /tmp/_.c
