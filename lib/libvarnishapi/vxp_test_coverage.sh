#!/bin/sh

./vxp_test -q '(*Error) or (BerespStatus >= 500)'
./vxp_test -q 'ReqHeader:user-agent ~ "iPod" and Timestamp:Resp[2] > 1'
