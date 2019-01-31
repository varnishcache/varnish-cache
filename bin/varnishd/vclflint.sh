#!/bin/sh
#
# Run flexelint on the VCL output
LIBS="-p vmod_path=/home/phk/Varnish/trunk/varnish-cache/lib/libvmod_std/.libs:/home/phk/Varnish/trunk/varnish-cache/lib/libvmod_debug/.libs:/home/phk/Varnish/trunk/varnish-cache/lib/libvmod_directors/.libs:/home/phk/Varnish/trunk/varnish-cache/lib/libvmod_purge/.libs:/home/phk/Varnish/trunk/varnish-cache/lib/libvmod_vtc/.libs:/home/phk/Varnish/trunk/varnish-cache/lib/libvmod_blob/.libs:/home/phk/Varnish/trunk/varnish-cache/lib/libvmod_unix/.libs:/home/phk/Varnish/trunk/varnish-cache/lib/libvmod_proxy/.libs"

if [ "x$1" = "x" ] ; then
	./varnishd $LIBS -C -b localhost > /tmp/_.c
elif [ -f $1 ] ; then
	./varnishd $LIBS -C -f $1 > /tmp/_.c
else
	echo "usage!" 1>&2
fi

flexelint vclflint.lnt /tmp/_.c
