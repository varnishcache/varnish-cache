#
# 

if [ ! -f vtree.h ] ; then
	echo "Run from include subdir"
	exit 1
fi

if [ ! -f /usr/src/sys/sys/tree.h ] ; then
	echo "You need a FreeBSD source tree in /usr/src"
	exit 1
fi

git diff vtree.h | git apply -R > /dev/null 2>&1 || true

GR=f6e54eb360a78856dcde930a00d9b2b3627309ab
(cd /usr/src/ && git show $GR:sys/sys/tree.h ) |
sed -E '
485a\
		AN(parent);			\\
s/_SYS_TREE_H_/_VTREE_H_/
s/SPLAY/VSPLAY/g
s/RB_/VRBT_/g
/(VRBT_FIND|VRBT_NFIND|VRBT_MINMAX)/{
s/struct name [*]/const struct name */
s/, struct type [*]/, const struct type */
}
/sys\/cdefs/d
s/__unused/v_unused_/
s/^        /	/g
' > _t

diff -uw _t vtree.h
mv _t vtree.h
