#!/bin/sh
#
# Run flexelint on the VCL output

./varnishd -C -b localhost > /tmp/_.c

flexelint vclflint.lnt /tmp/_.c 
