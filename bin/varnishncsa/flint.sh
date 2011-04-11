#!/bin/sh

T=/tmp/_$$
flexelint \
	-I/usr/include \
	-I. \
	-I../.. \
	-I../../include \
	../flint.lnt \
	flint.lnt \
	*.c > $T 2>&1

for t in Error Warning Info
do
	sed -n "/$t [0-9][0-9][0-9]:/s/.*\($t [0-9][0-9][0-9]\).*/\1/p" $T
done | awk '
$2 == 830	{ next }
$2 == 831	{ next }
	{
	i=$2"_"$1
	h[i]++
	n++
	}
END	{
	printf "%5d %s\n", n, "Total"
	for (i in h)
		printf "%5d %s\n", h[i], i
	}
' | sort -rn

cat $T
