#!/bin/sh

set -ex

./vxp_test -q '(*Error) or (BerespStatus >= 500)'
./vxp_test -q 'ReqHeader:user-agent ~ "iPod" and Timestamp:Resp[2] > 1'

! ./vxp_test -h 

! ./vxp_test -q 'The head and in frontal attack on an english writer' 
