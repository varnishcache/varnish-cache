#!/usr/bin/env python
#
# Very basic style-checker for include/tbl files.

from __future__ import print_function

import glob

def check_file(fn):
	s = 0
	ll = []
	for l in open(fn):
		ll.append(l)

	assert ll.pop(0)[:2] == "/*"

	while ll.pop(0) != " */\n":
		continue

	assert len(ll) > 5

	assert ll.pop(0) == "\n"
	assert ll.pop(0) == "/*lint -save -e525 -e539 */\n"
	assert ll.pop(0) == "\n"

	assert ll.pop(-1) == "/*lint -restore */\n"
	assert ll.pop(-1) == "\n"

	for i in range(0, len(ll) -1):
		assert ll[i] != "\n" or ll[i+1] != "\n"
		assert ll[i] != ")\n" or ll[i+1] == "\n" or ll[i+1][0] == "#"

	m = {}
	while len(ll) > 0:
		i = ll.pop(0)
		if i == "\n":
			continue
		l = i.lstrip()
		if l[0] >= 'A' and l[0] <= 'Z':
			j = l.split('(')
			m[j[0]] = "Called"
			l = l.split('//')[0]
			l = l.split('/*')[0]
			l = l.rstrip()
			if l[-1] != ')':
				while ll.pop(0) != ')\n':
					continue
		elif l[0] == "#":
			j = l[1:].lstrip().split()
			# print("#", j[0])
			if j[0] == "define":
				m[j[1].split("(")[0].strip()] = "Defined"
			if j[0] == "undef":
				m[j[1]] = "Undef"
			while l[-2:] == "\\\n":
				l = ll.pop(0)
		else:
			pass
			# print(l)
	rv = 0
	for i in m:
		if m[i] != "Undef":
			print("ERROR", fn, i, m[i])
			rv += 1
	return rv

rv = 0
for fn in glob.glob("*.h"):
	rv += check_file(fn)
if rv != 0:
	print(rv, "Errors")
exit(rv)
