#!/usr/bin/env python
#
# This script is in the public domain
#
# Run instructions:
#	varnishtest -W -iv -j <pick_a_number> > _.w
#	python witness.py
#	dot -Tpng /tmp/_.dot > /tmp/_.png

from __future__ import print_function

d = dict()
a = dict()

fi = open("_.w")
fo = open("/tmp/_.dot", "w")

fo.write('''digraph {
	#rotate="90"
	#page="8.2,11.7"
	size="8.2,11.7"
	rankdir="LR"
	node [fontname="Inconsolata", fontsize="10"]
	edge [fontname="Inconsolata", fontsize="10"]
''')

for i in fi:
	l = "ROOT"
	j = i.split()
	if len(j) < 8:
		continue
	if j[1][0] != 'v':
		continue
	if j[3] != "vsl|":
		continue
	if j[5] != "Witness":
		continue
	t = j[7:]
	tt = str(t)
	if tt in d:
		continue
	d[tt] = True
	for e in t:
		f = e.split(",")
		x = '"%s" -> "%s" [label="%s(%s)"]\n' % (l, f[0], f[1], f[2])
		if not x in a:
			a[x] = True
			fo.write(x)
		l = f[0]

fo.write("}\n")

