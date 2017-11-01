#!/usr/bin/env python

from __future__ import print_function

import os

def check(fn):
	l = []
	for i in open(fn):
		i = i.strip()
		if len(i) == 0:
			continue
		if i[0] != "#":
			continue
		if i.find("include") == -1:
			continue
		if i.find('"') == -1:
			l.append(i.split('<')[1].split('>')[0])
		else:
			l.append(i.split('"')[1])
	if "vrt.h" in l:
		vrt = l.index("vrt.h")
		if not "vdef.h" in l:
			print(fn, "vdef.h not included with vrt.h")
		vdef = l.index("vdef.h")
		if vdef > vrt:
			print(fn, "vdef.h included after vrt.h")
		for i in ("stddef.h", "stdint.h", "cache/cache.h", "cache.h"):
			if i in l:
				print(fn, i + " included with vrt.h")

	for i in ("cache/cache.h", "cache.h"):
		if i in l:
			for i in (
			    "stddef.h", "stdint.h", "vrt.h",
			    "math.h", "pthread.h", "stdarg.h", "sys/types.h",
			    "vdef.h", "miniobj.h", "vas.h", "vqueue.h",
			):
				if i in l:
					print(fn, i + " included with cache.h")

for (dir, dns, fns) in os.walk("."):
	for f in fns:
		if f[-2:] == ".c":
			check(dir + "/" + f)
