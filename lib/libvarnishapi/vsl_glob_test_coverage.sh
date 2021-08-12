#!/bin/sh

set -e
set -x

# wrong usage
! ./vsl_glob_test too many arguments

# built-in sanity checks
./vsl_glob_test

# coverage
./vsl_glob_test 'Req*'
./vsl_glob_test 'beresp*'
