#!/usr/bin/env python3

import re
import sys

#HPH(0x30, 0x00000000,  5)
regex = re.compile(r"^HPH\((.{4}), (.{10}), +(.{1,3})\)")

if len(sys.argv) != 2:
    print("{} takes one and only one argument".format(sys.argv[0]))
    sys.exit(2)

class sym:
    def __init__(self, bigval, bigvall, chr=0, esc=None):
        self.vall = bigvall % 8 if bigvall % 8 else 8
        self.val = bigval & ((1 << self.vall) - 1)
        self.pfx = (bigval >> self.vall)# & 0xff
        self.chr = chr
        self.esc = esc

tbls = {}
msl = {} # max sym length

f = open(sys.argv[1])
for l in f:
    grp = 1
    match = regex.match(l)
    if not match:
        continue

    char = int(match.group(grp), 16)
    grp += 1

    val = int(match.group(grp), 16)
    grp += 1

    vall = int(match.group(grp))

    s = sym(val, vall, char)
    if s.pfx not in tbls:
        tbls[s.pfx] = {}

    if s.val in tbls[s.pfx]:
        assert tbls[s.pfx][s.val].e
    tbls[s.pfx][s.val] = s

    # add the escape entry in the "previous" table
    if s.pfx:
        pp = s.pfx >> 8
        pv = s.pfx & 0xff
        if pp not in tbls:
            tbls[pp] = {}
        tbls[pp][pv] = sym(pv, 8, 0, "&tbl_{:x}".format(s.pfx))
f.close()

# add the EOS case
s = sym(63, 6, 0)
tbls[0xffffff][63] = s

print('''/* NB:  This file is machine generated, DO NOT EDIT!
 * edit 'huffman_input' instead
 */

struct stbl;

struct ssym {
    uint8_t csm;        /* bits consumed */
    uint8_t chr;        /* character */
    struct stbl *nxt;       /* next table */
};

struct stbl {
    unsigned msk;
    struct ssym *syms;
};
''')

for pfx in sorted(tbls.keys(), reverse=True):
    msl = max([x.vall for x in tbls[pfx].values()])
    for s in tbls[pfx].values():
        s.val = s.val << (msl - s.vall)

    tbl = sorted(tbls[pfx].values(), key=lambda x: x.val)
    print("\nstatic struct ssym sym_{:x}_array[] = {{".format(pfx))
    for s in tbl:
        for j in range(2 ** (msl - s.vall)):
            print("{} {{{}, {:3d}, {}}},".format(
                "\t     " if j else "/* idx {:3d} */".format(s.val + j),
                s.vall, s.chr % 256,
                s.esc if s.esc else "NULL"))
    print('''}};

static struct stbl tbl_{:x} = {{
    {},
    sym_{:x}_array
}};'''.format(pfx, msl, pfx))
