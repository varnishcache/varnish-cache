#!/bin/sh

# A client side script to test the ESI parsing, see s.sh for serverside

set -e

echo "[2J"
while true
do
	sleep 1
	echo "[H"
	fetch -o - -q http://localhost:8080/ | hexdump -C |
		 sed 's/$/[K/'
	echo "[J"
done
