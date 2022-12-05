/*	$NetBSD: tree.h,v 1.8 2004/03/28 19:38:30 provos Exp $	*/
/*	$OpenBSD: tree.h,v 1.7 2002/10/17 21:51:54 art Exp $	*/
/* $FreeBSD$ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2002 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef	_VTREE_H_
#define	_VTREE_H_

/* XXX
 * Enable -Wall with gcc -O2
 */
#pragma GCC diagnostic ignored "-Wstrict-aliasing"

/*
 * This file defines data structures for different types of trees:
 * splay trees and rank-balanced trees.
 *
 * A splay tree is a self-organizing data structure.  Every operation
 * on the tree causes a splay to happen.  The splay moves the requested
 * node to the root of the tree and partly rebalances it.
 *
 * This has the benefit that request locality causes faster lookups as
 * the requested nodes move to the top of the tree.  On the other hand,
 * every lookup causes memory writes.
 *
 * The Balance Theorem bounds the total access time for m operations
 * and n inserts on an initially empty tree as O((m + n)lg n).  The
 * amortized cost for a sequence of m accesses to a splay tree is O(lg n);
 *
 * A rank-balanced tree is a binary search tree with an integer
 * rank-difference as an attribute of each pointer from parent to child.
 * The sum of the rank-differences on any path from a node down to null is
 * the same, and defines the rank of that node. The rank of the null node
 * is -1.
 *
 * Different additional conditions define different sorts of balanced trees,
 * including "red-black" and "AVL" trees.  The set of conditions applied here
 * are the "weak-AVL" conditions of Haeupler, Sen and Tarjan presented in in
 * "Rank Balanced Trees", ACM Transactions on Algorithms Volume 11 Issue 4 June
 * 2015 Article No.: 30pp 1–26 https://doi.org/10.1145/2689412 (the HST paper):
 *	- every rank-difference is 1 or 2.
 *	- the rank of any leaf is 1.
 *
 * For historical reasons, rank differences that are even are associated
 * with the color red (Rank-Even-Difference), and the child that a red edge
 * points to is called a red child.
 *
 * Every operation on a rank-balanced tree is bounded as O(lg n).
 * The maximum height of a rank-balanced tree is 2lg (n+1).
 */

#define VSPLAY_HEAD(name, type)						\
struct name {								\
	struct type *sph_root; /* root of the tree */			\
}

#define VSPLAY_INITIALIZER(root)						\
	{ NULL }

#define VSPLAY_INIT(root) do {						\
	(root)->sph_root = NULL;					\
} while (/*CONSTCOND*/ 0)

#define VSPLAY_ENTRY(type)						\
struct {								\
	struct type *spe_left; /* left element */			\
	struct type *spe_right; /* right element */			\
}

#define VSPLAY_LEFT(elm, field)		(elm)->field.spe_left
#define VSPLAY_RIGHT(elm, field)		(elm)->field.spe_right
#define VSPLAY_ROOT(head)		(head)->sph_root
#define VSPLAY_EMPTY(head)		(VSPLAY_ROOT(head) == NULL)

/* VSPLAY_ROTATE_{LEFT,RIGHT} expect that tmp hold VSPLAY_{RIGHT,LEFT} */
#define VSPLAY_ROTATE_RIGHT(head, tmp, field) do {			\
	VSPLAY_LEFT((head)->sph_root, field) = VSPLAY_RIGHT(tmp, field);	\
	VSPLAY_RIGHT(tmp, field) = (head)->sph_root;			\
	(head)->sph_root = tmp;						\
} while (/*CONSTCOND*/ 0)

#define VSPLAY_ROTATE_LEFT(head, tmp, field) do {			\
	VSPLAY_RIGHT((head)->sph_root, field) = VSPLAY_LEFT(tmp, field);	\
	VSPLAY_LEFT(tmp, field) = (head)->sph_root;			\
	(head)->sph_root = tmp;						\
} while (/*CONSTCOND*/ 0)

#define VSPLAY_LINKLEFT(head, tmp, field) do {				\
	VSPLAY_LEFT(tmp, field) = (head)->sph_root;			\
	tmp = (head)->sph_root;						\
	(head)->sph_root = VSPLAY_LEFT((head)->sph_root, field);		\
} while (/*CONSTCOND*/ 0)

#define VSPLAY_LINKRIGHT(head, tmp, field) do {				\
	VSPLAY_RIGHT(tmp, field) = (head)->sph_root;			\
	tmp = (head)->sph_root;						\
	(head)->sph_root = VSPLAY_RIGHT((head)->sph_root, field);	\
} while (/*CONSTCOND*/ 0)

#define VSPLAY_ASSEMBLE(head, node, left, right, field) do {		\
	VSPLAY_RIGHT(left, field) = VSPLAY_LEFT((head)->sph_root, field);	\
	VSPLAY_LEFT(right, field) = VSPLAY_RIGHT((head)->sph_root, field);\
	VSPLAY_LEFT((head)->sph_root, field) = VSPLAY_RIGHT(node, field);	\
	VSPLAY_RIGHT((head)->sph_root, field) = VSPLAY_LEFT(node, field);	\
} while (/*CONSTCOND*/ 0)

/* Generates prototypes and inline functions */

#define VSPLAY_PROTOTYPE(name, type, field, cmp)				\
void name##_VSPLAY(struct name *, struct type *);			\
void name##_VSPLAY_MINMAX(struct name *, int);				\
struct type *name##_VSPLAY_INSERT(struct name *, struct type *);		\
struct type *name##_VSPLAY_REMOVE(struct name *, struct type *);		\
									\
/* Finds the node with the same key as elm */				\
static v_unused_ __inline struct type *					\
name##_VSPLAY_FIND(struct name *head, struct type *elm)			\
{									\
	if (VSPLAY_EMPTY(head))						\
		return(NULL);						\
	name##_VSPLAY(head, elm);					\
	if ((cmp)(elm, (head)->sph_root) == 0)				\
		return (head->sph_root);				\
	return (NULL);							\
}									\
									\
static v_unused_ __inline struct type *					\
name##_VSPLAY_NEXT(struct name *head, struct type *elm)			\
{									\
	name##_VSPLAY(head, elm);					\
	if (VSPLAY_RIGHT(elm, field) != NULL) {				\
		elm = VSPLAY_RIGHT(elm, field);				\
		while (VSPLAY_LEFT(elm, field) != NULL) {		\
			elm = VSPLAY_LEFT(elm, field);			\
		}							\
	} else								\
		elm = NULL;						\
	return (elm);							\
}									\
									\
static v_unused_ __inline struct type *					\
name##_VSPLAY_MIN_MAX(struct name *head, int val)			\
{									\
	name##_VSPLAY_MINMAX(head, val);					\
	return (VSPLAY_ROOT(head));					\
}

/* Main splay operation.
 * Moves node close to the key of elm to top
 */
#define VSPLAY_GENERATE(name, type, field, cmp)				\
struct type *								\
name##_VSPLAY_INSERT(struct name *head, struct type *elm)		\
{									\
    if (VSPLAY_EMPTY(head)) {						\
	    VSPLAY_LEFT(elm, field) = VSPLAY_RIGHT(elm, field) = NULL;	\
    } else {								\
	    __typeof(cmp(NULL, NULL)) __comp;				\
	    name##_VSPLAY(head, elm);					\
	    __comp = (cmp)(elm, (head)->sph_root);			\
	    if (__comp < 0) {						\
		    VSPLAY_LEFT(elm, field) = VSPLAY_LEFT((head)->sph_root, field);\
		    VSPLAY_RIGHT(elm, field) = (head)->sph_root;		\
		    VSPLAY_LEFT((head)->sph_root, field) = NULL;		\
	    } else if (__comp > 0) {					\
		    VSPLAY_RIGHT(elm, field) = VSPLAY_RIGHT((head)->sph_root, field);\
		    VSPLAY_LEFT(elm, field) = (head)->sph_root;		\
		    VSPLAY_RIGHT((head)->sph_root, field) = NULL;	\
	    } else							\
		    return ((head)->sph_root);				\
    }									\
    (head)->sph_root = (elm);						\
    return (NULL);							\
}									\
									\
struct type *								\
name##_VSPLAY_REMOVE(struct name *head, struct type *elm)		\
{									\
	struct type *__tmp;						\
	if (VSPLAY_EMPTY(head))						\
		return (NULL);						\
	name##_VSPLAY(head, elm);					\
	if ((cmp)(elm, (head)->sph_root) == 0) {			\
		if (VSPLAY_LEFT((head)->sph_root, field) == NULL) {	\
			(head)->sph_root = VSPLAY_RIGHT((head)->sph_root, field);\
		} else {						\
			__tmp = VSPLAY_RIGHT((head)->sph_root, field);	\
			(head)->sph_root = VSPLAY_LEFT((head)->sph_root, field);\
			name##_VSPLAY(head, elm);			\
			VSPLAY_RIGHT((head)->sph_root, field) = __tmp;	\
		}							\
		return (elm);						\
	}								\
	return (NULL);							\
}									\
									\
void									\
name##_VSPLAY(struct name *head, struct type *elm)			\
{									\
	struct type __node, *__left, *__right, *__tmp;			\
	__typeof(cmp(NULL, NULL)) __comp;				\
\
	VSPLAY_LEFT(&__node, field) = VSPLAY_RIGHT(&__node, field) = NULL;\
	__left = __right = &__node;					\
\
	while ((__comp = (cmp)(elm, (head)->sph_root)) != 0) {		\
		if (__comp < 0) {					\
			__tmp = VSPLAY_LEFT((head)->sph_root, field);	\
			if (__tmp == NULL)				\
				break;					\
			if ((cmp)(elm, __tmp) < 0){			\
				VSPLAY_ROTATE_RIGHT(head, __tmp, field);	\
				if (VSPLAY_LEFT((head)->sph_root, field) == NULL)\
					break;				\
			}						\
			VSPLAY_LINKLEFT(head, __right, field);		\
		} else if (__comp > 0) {				\
			__tmp = VSPLAY_RIGHT((head)->sph_root, field);	\
			if (__tmp == NULL)				\
				break;					\
			if ((cmp)(elm, __tmp) > 0){			\
				VSPLAY_ROTATE_LEFT(head, __tmp, field);	\
				if (VSPLAY_RIGHT((head)->sph_root, field) == NULL)\
					break;				\
			}						\
			VSPLAY_LINKRIGHT(head, __left, field);		\
		}							\
	}								\
	VSPLAY_ASSEMBLE(head, &__node, __left, __right, field);		\
}									\
									\
/* Splay with either the minimum or the maximum element			\
 * Used to find minimum or maximum element in tree.			\
 */									\
void name##_VSPLAY_MINMAX(struct name *head, int __comp) \
{									\
	struct type __node, *__left, *__right, *__tmp;			\
\
	VSPLAY_LEFT(&__node, field) = VSPLAY_RIGHT(&__node, field) = NULL;\
	__left = __right = &__node;					\
\
	while (1) {							\
		if (__comp < 0) {					\
			__tmp = VSPLAY_LEFT((head)->sph_root, field);	\
			if (__tmp == NULL)				\
				break;					\
			if (__comp < 0){				\
				VSPLAY_ROTATE_RIGHT(head, __tmp, field);	\
				if (VSPLAY_LEFT((head)->sph_root, field) == NULL)\
					break;				\
			}						\
			VSPLAY_LINKLEFT(head, __right, field);		\
		} else if (__comp > 0) {				\
			__tmp = VSPLAY_RIGHT((head)->sph_root, field);	\
			if (__tmp == NULL)				\
				break;					\
			if (__comp > 0) {				\
				VSPLAY_ROTATE_LEFT(head, __tmp, field);	\
				if (VSPLAY_RIGHT((head)->sph_root, field) == NULL)\
					break;				\
			}						\
			VSPLAY_LINKRIGHT(head, __left, field);		\
		}							\
	}								\
	VSPLAY_ASSEMBLE(head, &__node, __left, __right, field);		\
}

#define VSPLAY_NEGINF	-1
#define VSPLAY_INF	1

#define VSPLAY_INSERT(name, x, y)	name##_VSPLAY_INSERT(x, y)
#define VSPLAY_REMOVE(name, x, y)	name##_VSPLAY_REMOVE(x, y)
#define VSPLAY_FIND(name, x, y)		name##_VSPLAY_FIND(x, y)
#define VSPLAY_NEXT(name, x, y)		name##_VSPLAY_NEXT(x, y)
#define VSPLAY_MIN(name, x)		(VSPLAY_EMPTY(x) ? NULL	\
					: name##_VSPLAY_MIN_MAX(x, VSPLAY_NEGINF))
#define VSPLAY_MAX(name, x)		(VSPLAY_EMPTY(x) ? NULL	\
					: name##_VSPLAY_MIN_MAX(x, VSPLAY_INF))

#define VSPLAY_FOREACH(x, name, head)					\
	for ((x) = VSPLAY_MIN(name, head);				\
	     (x) != NULL;						\
	     (x) = VSPLAY_NEXT(name, head, x))

/* Macros that define a rank-balanced tree */
#define VRBT_HEAD(name, type)						\
struct name {								\
	struct type *rbh_root; /* root of the tree */			\
}

#define VRBT_INITIALIZER(root)						\
	{ NULL }

#define VRBT_INIT(root) do {						\
	(root)->rbh_root = NULL;					\
} while (/*CONSTCOND*/ 0)

#define VRBT_ENTRY(type)							\
struct {								\
	struct type *rbe_link[3];					\
}

/*
 * With the expectation that any object of struct type has an
 * address that is a multiple of 4, and that therefore the
 * 2 least significant bits of a pointer to struct type are
 * always zero, this implementation sets those bits to indicate
 * that the left or right child of the tree node is "red".
 */
#define _VRBT_LINK(elm, dir, field)	(elm)->field.rbe_link[dir]
#define _VRBT_UP(elm, field)		_VRBT_LINK(elm, 0, field)
#define _VRBT_L				((uintptr_t)1)
#define _VRBT_R				((uintptr_t)2)
#define _VRBT_LR				((uintptr_t)3)
#define _VRBT_BITS(elm)			(*(uintptr_t *)&elm)
#define _VRBT_BITSUP(elm, field)		_VRBT_BITS(_VRBT_UP(elm, field))
#define _VRBT_PTR(elm)			(__typeof(elm))			\
					((uintptr_t)elm & ~_VRBT_LR)

#define VRBT_PARENT(elm, field)		_VRBT_PTR(_VRBT_UP(elm, field))
#define VRBT_LEFT(elm, field)		_VRBT_LINK(elm, _VRBT_L, field)
#define VRBT_RIGHT(elm, field)		_VRBT_LINK(elm, _VRBT_R, field)
#define VRBT_ROOT(head)			(head)->rbh_root
#define VRBT_EMPTY(head)			(VRBT_ROOT(head) == NULL)

#define VRBT_SET_PARENT(dst, src, field) do {				\
	_VRBT_BITSUP(dst, field) = (uintptr_t)src |			\
	    (_VRBT_BITSUP(dst, field) & _VRBT_LR);				\
} while (/*CONSTCOND*/ 0)

#define VRBT_SET(elm, parent, field) do {					\
	_VRBT_UP(elm, field) = parent;					\
	VRBT_LEFT(elm, field) = VRBT_RIGHT(elm, field) = NULL;		\
} while (/*CONSTCOND*/ 0)

/*
 * Either VRBT_AUGMENT or VRBT_AUGMENT_CHECK is invoked in a loop at the root of
 * every modified subtree, from the bottom up to the root, to update augmented
 * node data.  VRBT_AUGMENT_CHECK returns true only when the update changes the
 * node data, so that updating can be stopped short of the root when it returns
 * false.
 */
#ifndef VRBT_AUGMENT_CHECK
#ifndef VRBT_AUGMENT
#define VRBT_AUGMENT_CHECK(x) 0
#else
#define VRBT_AUGMENT_CHECK(x) (VRBT_AUGMENT(x), 1)
#endif
#endif

#define VRBT_UPDATE_AUGMENT(elm, field) do {				\
	__typeof(elm) rb_update_tmp = (elm);				\
	while (VRBT_AUGMENT_CHECK(rb_update_tmp) &&			\
	    (rb_update_tmp = VRBT_PARENT(rb_update_tmp, field)) != NULL)	\
		;							\
} while (0)

#define VRBT_SWAP_CHILD(head, par, out, in, field) do {			\
	if (par == NULL)						\
		VRBT_ROOT(head) = (in);					\
	else if ((out) == VRBT_LEFT(par, field))				\
		VRBT_LEFT(par, field) = (in);				\
	else								\
		VRBT_RIGHT(par, field) = (in);				\
} while (/*CONSTCOND*/ 0)

/*
 * VRBT_ROTATE macro partially restructures the tree to improve balance. In the
 * case when dir is _VRBT_L, tmp is a right child of elm.  After rotation, elm
 * is a left child of tmp, and the subtree that represented the items between
 * them, which formerly hung to the left of tmp now hangs to the right of elm.
 * The parent-child relationship between elm and its former parent is not
 * changed; where this macro once updated those fields, that is now left to the
 * caller of VRBT_ROTATE to clean up, so that a pair of rotations does not twice
 * update the same pair of pointer fields with distinct values.
 */
#define VRBT_ROTATE(elm, tmp, dir, field) do {				\
	if ((_VRBT_LINK(elm, dir ^ _VRBT_LR, field) =			\
	    _VRBT_LINK(tmp, dir, field)) != NULL)				\
		VRBT_SET_PARENT(_VRBT_LINK(tmp, dir, field), elm, field);	\
	_VRBT_LINK(tmp, dir, field) = (elm);				\
	VRBT_SET_PARENT(elm, tmp, field);					\
} while (/*CONSTCOND*/ 0)

/* Generates prototypes and inline functions */
#define	VRBT_PROTOTYPE(name, type, field, cmp)				\
	VRBT_PROTOTYPE_INTERNAL(name, type, field, cmp,)
#define	VRBT_PROTOTYPE_STATIC(name, type, field, cmp)			\
	VRBT_PROTOTYPE_INTERNAL(name, type, field, cmp, v_unused_ static)
#define VRBT_PROTOTYPE_INTERNAL(name, type, field, cmp, attr)		\
	VRBT_PROTOTYPE_RANK(name, type, attr)				\
	VRBT_PROTOTYPE_INSERT_COLOR(name, type, attr);			\
	VRBT_PROTOTYPE_REMOVE_COLOR(name, type, attr);			\
	VRBT_PROTOTYPE_INSERT_FINISH(name, type, attr);			\
	VRBT_PROTOTYPE_INSERT(name, type, attr);				\
	VRBT_PROTOTYPE_REMOVE(name, type, attr);				\
	VRBT_PROTOTYPE_FIND(name, type, attr);				\
	VRBT_PROTOTYPE_NFIND(name, type, attr);				\
	VRBT_PROTOTYPE_NEXT(name, type, attr);				\
	VRBT_PROTOTYPE_INSERT_NEXT(name, type, attr);			\
	VRBT_PROTOTYPE_PREV(name, type, attr);				\
	VRBT_PROTOTYPE_INSERT_PREV(name, type, attr);			\
	VRBT_PROTOTYPE_MINMAX(name, type, attr);				\
	VRBT_PROTOTYPE_REINSERT(name, type, attr);
#ifdef _VRBT_DIAGNOSTIC
#define VRBT_PROTOTYPE_RANK(name, type, attr)				\
	attr int name##_VRBT_RANK(struct type *);
#else
#define VRBT_PROTOTYPE_RANK(name, type, attr)
#endif
#define VRBT_PROTOTYPE_INSERT_COLOR(name, type, attr)			\
	attr struct type *name##_VRBT_INSERT_COLOR(struct name *,		\
	    struct type *, struct type *)
#define VRBT_PROTOTYPE_REMOVE_COLOR(name, type, attr)			\
	attr struct type *name##_VRBT_REMOVE_COLOR(struct name *,		\
	    struct type *, struct type *)
#define VRBT_PROTOTYPE_REMOVE(name, type, attr)				\
	attr struct type *name##_VRBT_REMOVE(struct name *, struct type *)
#define VRBT_PROTOTYPE_INSERT_FINISH(name, type, attr)			\
	attr struct type *name##_VRBT_INSERT_FINISH(struct name *,	\
	    struct type *, struct type **, struct type *)
#define VRBT_PROTOTYPE_INSERT(name, type, attr)				\
	attr struct type *name##_VRBT_INSERT(struct name *, struct type *)
#define VRBT_PROTOTYPE_FIND(name, type, attr)				\
	attr const struct type *name##_VRBT_FIND(const struct name *, const struct type *)
#define VRBT_PROTOTYPE_NFIND(name, type, attr)				\
	attr struct type *name##_VRBT_NFIND(struct name *, struct type *)
#define VRBT_PROTOTYPE_NEXT(name, type, attr)				\
	attr struct type *name##_VRBT_NEXT(struct type *)
#define VRBT_PROTOTYPE_INSERT_NEXT(name, type, attr)			\
	attr struct type *name##_VRBT_INSERT_NEXT(struct name *,		\
	    struct type *, struct type *)
#define VRBT_PROTOTYPE_PREV(name, type, attr)				\
	attr struct type *name##_VRBT_PREV(struct type *)
#define VRBT_PROTOTYPE_INSERT_PREV(name, type, attr)			\
	attr struct type *name##_VRBT_INSERT_PREV(struct name *,		\
	    struct type *, struct type *)
#define VRBT_PROTOTYPE_MINMAX(name, type, attr)				\
	attr const struct type *name##_VRBT_MINMAX(struct name *, int)
#define VRBT_PROTOTYPE_REINSERT(name, type, attr)			\
	attr struct type *name##_VRBT_REINSERT(struct name *, struct type *)

/* Main rb operation.
 * Moves node close to the key of elm to top
 */
#define	VRBT_GENERATE(name, type, field, cmp)				\
	VRBT_GENERATE_INTERNAL(name, type, field, cmp,)
#define	VRBT_GENERATE_STATIC(name, type, field, cmp)			\
	VRBT_GENERATE_INTERNAL(name, type, field, cmp, v_unused_ static)
#define VRBT_GENERATE_INTERNAL(name, type, field, cmp, attr)		\
	VRBT_GENERATE_RANK(name, type, field, attr)			\
	VRBT_GENERATE_INSERT_COLOR(name, type, field, attr)		\
	VRBT_GENERATE_REMOVE_COLOR(name, type, field, attr)		\
	VRBT_GENERATE_INSERT_FINISH(name, type, field, attr)		\
	VRBT_GENERATE_INSERT(name, type, field, cmp, attr)		\
	VRBT_GENERATE_REMOVE(name, type, field, attr)			\
	VRBT_GENERATE_FIND(name, type, field, cmp, attr)			\
	VRBT_GENERATE_NFIND(name, type, field, cmp, attr)			\
	VRBT_GENERATE_NEXT(name, type, field, attr)			\
	VRBT_GENERATE_INSERT_NEXT(name, type, field, cmp, attr)		\
	VRBT_GENERATE_PREV(name, type, field, attr)			\
	VRBT_GENERATE_INSERT_PREV(name, type, field, cmp, attr)		\
	VRBT_GENERATE_MINMAX(name, type, field, attr)			\
	VRBT_GENERATE_REINSERT(name, type, field, cmp, attr)

#ifdef _VRBT_DIAGNOSTIC
#ifndef VRBT_AUGMENT
#define _VRBT_AUGMENT_VERIFY(x) VRBT_AUGMENT_CHECK(x)
#else
#define _VRBT_AUGMENT_VERIFY(x) 0
#endif
#define VRBT_GENERATE_RANK(name, type, field, attr)			\
/*									\
 * Return the rank of the subtree rooted at elm, or -1 if the subtree	\
 * is not rank-balanced, or has inconsistent augmentation data.
 */									\
attr int								\
name##_VRBT_RANK(struct type *elm)					\
{									\
	struct type *left, *right, *up;					\
	int left_rank, right_rank;					\
									\
	if (elm == NULL)						\
		return (0);						\
	up = _VRBT_UP(elm, field);					\
	left = VRBT_LEFT(elm, field);					\
	left_rank = ((_VRBT_BITS(up) & _VRBT_L) ? 2 : 1) +			\
	    name##_VRBT_RANK(left);					\
	right = VRBT_RIGHT(elm, field);					\
	right_rank = ((_VRBT_BITS(up) & _VRBT_R) ? 2 : 1) +			\
	    name##_VRBT_RANK(right);					\
	if (left_rank != right_rank ||					\
	    (left_rank == 2 && left == NULL && right == NULL) ||	\
	    _VRBT_AUGMENT_VERIFY(elm))					\
		return (-1);						\
	return (left_rank);						\
}
#else
#define VRBT_GENERATE_RANK(name, type, field, attr)
#endif

#define VRBT_GENERATE_INSERT_COLOR(name, type, field, attr)		\
attr struct type *							\
name##_VRBT_INSERT_COLOR(struct name *head,				\
    struct type *parent, struct type *elm)				\
{									\
	/*								\
	 * Initially, elm is a leaf.  Either its parent was previously	\
	 * a leaf, with two black null children, or an interior node	\
	 * with a black non-null child and a red null child. The        \
	 * balance criterion "the rank of any leaf is 1" precludes the  \
	 * possibility of two red null children for the initial parent. \
	 * So the first loop iteration cannot lead to accessing an      \
	 * uninitialized 'child', and a later iteration can only happen \
	 * when a value has been assigned to 'child' in the previous    \
	 * one.								\
	 */								\
	struct type *child = NULL, *child_up, *gpar;				\
	uintptr_t elmdir, sibdir;					\
									\
	do {								\
		/* the rank of the tree rooted at elm grew */		\
		gpar = _VRBT_UP(parent, field);				\
		elmdir = VRBT_RIGHT(parent, field) == elm ? _VRBT_R : _VRBT_L; \
		if (_VRBT_BITS(gpar) & elmdir) {				\
			/* shorten the parent-elm edge to rebalance */	\
			_VRBT_BITSUP(parent, field) ^= elmdir;		\
			return (NULL);					\
		}							\
		sibdir = elmdir ^ _VRBT_LR;				\
		/* the other edge must change length */			\
		_VRBT_BITSUP(parent, field) ^= sibdir;			\
		if ((_VRBT_BITS(gpar) & _VRBT_LR) == 0) {			\
			/* both edges now short, retry from parent */	\
			child = elm;					\
			elm = parent;					\
			continue;					\
		}							\
		_VRBT_UP(parent, field) = gpar = _VRBT_PTR(gpar);		\
		if (_VRBT_BITSUP(elm, field) & elmdir) {			\
			/*						\
			 * Exactly one of the edges descending from elm \
			 * is long. The long one is in the same		\
			 * direction as the edge from parent to elm,	\
			 * so change that by rotation.  The edge from	\
			 * parent to z was shortened above.  Shorten	\
			 * the long edge down from elm, and adjust	\
			 * other edge lengths based on the downward	\
			 * edges from 'child'.				\
			 *						\
			 *	     par		 par		\
			 *	    ╱	╲		╱   ╲		\
			 *	  elm	 z	       ╱     z		\
			 *	 ╱  ╲		     child		\
			 *	╱  child	     ╱	 ╲		\
			 *     ╱   ╱  ╲		   elm	  ╲		\
			 *    w	  ╱    ╲	  ╱   ╲    y		\
			 *	 x      y	 w     ╲		\
			 *				x		\
			 */						\
			VRBT_ROTATE(elm, child, elmdir, field);		\
			child_up = _VRBT_UP(child, field);		\
			if (_VRBT_BITS(child_up) & sibdir)		\
				_VRBT_BITSUP(parent, field) ^= elmdir;	\
			if (_VRBT_BITS(child_up) & elmdir)		\
				_VRBT_BITSUP(elm, field) ^= _VRBT_LR;	\
			else						\
				_VRBT_BITSUP(elm, field) ^= elmdir;	\
			/* if child is a leaf, don't augment elm,	\
			 * since it is restored to be a leaf again. */	\
			if ((_VRBT_BITS(child_up) & _VRBT_LR) == 0)		\
				elm = child;				\
		} else							\
			child = elm;					\
									\
		/*							\
		 * The long edge descending from 'child' points back	\
		 * in the direction of 'parent'. Rotate to make		\
		 * 'parent' a child of 'child', then make both edges	\
		 * of 'child' short to rebalance.			\
		 *							\
		 *	     par		 child			\
		 *	    ╱	╲		╱     ╲			\
		 *	   ╱	 z	       x       par		\
		 *	child			      ╱	  ╲		\
		 *	 ╱  ╲			     ╱	   z		\
		 *	x    ╲			    y			\
		 *	      y						\
		 */							\
		VRBT_ROTATE(parent, child, sibdir, field);		\
		_VRBT_UP(child, field) = gpar;				\
		VRBT_SWAP_CHILD(head, gpar, parent, child, field);	\
		/*							\
		 * Elements rotated down have new, smaller subtrees,	\
		 * so update augmentation for them.			\
		 */							\
		if (elm != child)					\
			(void)VRBT_AUGMENT_CHECK(elm);			\
		(void)VRBT_AUGMENT_CHECK(parent);				\
		return (child);						\
	} while ((parent = gpar) != NULL);				\
	return (NULL);							\
}

#ifndef VRBT_STRICT_HST
/*
 * In REMOVE_COLOR, the HST paper, in figure 3, in the single-rotate case, has
 * 'parent' with one higher rank, and then reduces its rank if 'parent' has
 * become a leaf.  This implementation always has the parent in its new position
 * with lower rank, to avoid the leaf check.  Define VRBT_STRICT_HST to 1 to get
 * the behavior that HST describes.
 */
#define VRBT_STRICT_HST 0
#endif

#define VRBT_GENERATE_REMOVE_COLOR(name, type, field, attr)		\
attr struct type *							\
name##_VRBT_REMOVE_COLOR(struct name *head,				\
    struct type *parent, struct type *elm)				\
{									\
	struct type *gpar, *sib, *up;					\
	uintptr_t elmdir, sibdir;					\
									\
	if (VRBT_RIGHT(parent, field) == elm &&				\
	    VRBT_LEFT(parent, field) == elm) {				\
		/* Deleting a leaf that is an only-child creates a	\
		 * rank-2 leaf. Demote that leaf. */			\
		_VRBT_UP(parent, field) = _VRBT_PTR(_VRBT_UP(parent, field));	\
		elm = parent;						\
		if ((parent = _VRBT_UP(elm, field)) == NULL)		\
			return (NULL);					\
	}								\
	do {								\
		/* the rank of the tree rooted at elm shrank */		\
		gpar = _VRBT_UP(parent, field);				\
		elmdir = VRBT_RIGHT(parent, field) == elm ? _VRBT_R : _VRBT_L; \
		_VRBT_BITS(gpar) ^= elmdir;				\
		if (_VRBT_BITS(gpar) & elmdir) {				\
			/* lengthen the parent-elm edge to rebalance */	\
			_VRBT_UP(parent, field) = gpar;			\
			return (NULL);					\
		}							\
		if (_VRBT_BITS(gpar) & _VRBT_LR) {				\
			/* shorten other edge, retry from parent */	\
			_VRBT_BITS(gpar) ^= _VRBT_LR;			\
			_VRBT_UP(parent, field) = gpar;			\
			gpar = _VRBT_PTR(gpar);				\
			continue;					\
		}							\
		sibdir = elmdir ^ _VRBT_LR;				\
		sib = _VRBT_LINK(parent, sibdir, field);			\
		up = _VRBT_UP(sib, field);				\
		_VRBT_BITS(up) ^= _VRBT_LR;					\
		if ((_VRBT_BITS(up) & _VRBT_LR) == 0) {			\
			/* shorten edges descending from sib, retry */	\
			_VRBT_UP(sib, field) = up;			\
			continue;					\
		}							\
		if ((_VRBT_BITS(up) & sibdir) == 0) {			\
			/*						\
			 * The edge descending from 'sib' away from	\
			 * 'parent' is long.  The short edge descending	\
			 * from 'sib' toward 'parent' points to 'elm*'	\
			 * Rotate to make 'sib' a child of 'elm*'	\
			 * then adjust the lengths of the edges		\
			 * descending from 'sib' and 'elm*'.		\
			 *						\
			 *	     par		 par		\
			 *	    ╱	╲		╱   ╲		\
			 *	   ╱	sib	      elm    ╲		\
			 *	  ╱	/ ╲	            elm*	\
			 *	elm   elm* ╲	            ╱  ╲	\
			 *	      ╱	╲   ╲		   ╱    ╲	\
			 *	     ╱   ╲   z		  ╱      ╲	\
			 *	    x	  y		 x      sib	\
			 *				        ╱  ╲	\
			 *				       ╱    z	\
			 *				      y		\
			 */						\
			elm = _VRBT_LINK(sib, elmdir, field);		\
			/* elm is a 1-child.  First rotate at elm. */	\
			VRBT_ROTATE(sib, elm, sibdir, field);		\
			up = _VRBT_UP(elm, field);			\
			_VRBT_BITSUP(parent, field) ^=			\
			    (_VRBT_BITS(up) & elmdir) ? _VRBT_LR : elmdir;	\
			_VRBT_BITSUP(sib, field) ^=			\
			    (_VRBT_BITS(up) & sibdir) ? _VRBT_LR : sibdir;	\
			_VRBT_BITSUP(elm, field) |= _VRBT_LR;		\
		} else {						\
			if ((_VRBT_BITS(up) & elmdir) == 0 &&		\
			    VRBT_STRICT_HST && elm != NULL) {		\
				/* if parent does not become a leaf,	\
				   do not demote parent yet. */		\
				_VRBT_BITSUP(parent, field) ^= sibdir;	\
				_VRBT_BITSUP(sib, field) ^= _VRBT_LR;	\
			} else if ((_VRBT_BITS(up) & elmdir) == 0) {	\
				/* demote parent. */			\
				_VRBT_BITSUP(parent, field) ^= elmdir;	\
				_VRBT_BITSUP(sib, field) ^= sibdir;	\
			} else						\
				_VRBT_BITSUP(sib, field) ^= sibdir;	\
			elm = sib;					\
		}							\
									\
		/*							\
		 * The edge descending from 'elm' away from 'parent'	\
		 * is short.  Rotate to make 'parent' a child of 'elm', \
		 * then lengthen the short edges descending from	\
		 * 'parent' and 'elm' to rebalance.			\
		 *							\
		 *	     par		 elm			\
		 *	    ╱	╲		╱   ╲			\
		 *	   e	 ╲	       ╱     ╲			\
		 *		 elm	      ╱	      ╲			\
		 *		╱  ╲	    par	       s		\
		 *	       ╱    ╲	   ╱   ╲			\
		 *	      ╱	     ╲	  e	╲			\
		 *	     x	      s		 x			\
		 */							\
		VRBT_ROTATE(parent, elm, elmdir, field);			\
		VRBT_SET_PARENT(elm, gpar, field);			\
		VRBT_SWAP_CHILD(head, gpar, parent, elm, field);		\
		/*							\
		 * An element rotated down, but not into the search	\
		 * path has a new, smaller subtree, so update		\
		 * augmentation for it.					\
		 */							\
		if (sib != elm)						\
			(void)VRBT_AUGMENT_CHECK(sib);			\
		return (parent);					\
	} while (elm = parent, (parent = gpar) != NULL);		\
	return (NULL);							\
}

#define _VRBT_AUGMENT_WALK(elm, match, field)				\
do {									\
	if (match == elm)						\
		match = NULL;						\
} while (VRBT_AUGMENT_CHECK(elm) &&					\
    (elm = VRBT_PARENT(elm, field)) != NULL)

#define VRBT_GENERATE_REMOVE(name, type, field, attr)			\
attr struct type *							\
name##_VRBT_REMOVE(struct name *head, struct type *out)			\
{									\
	struct type *child = NULL, *in, *opar, *parent;			\
									\
	child = VRBT_LEFT(out, field);					\
	in = VRBT_RIGHT(out, field);					\
	opar = _VRBT_UP(out, field);					\
	if (in == NULL || child == NULL) {				\
		in = child = (in == NULL ? child : in);			\
		parent = opar = _VRBT_PTR(opar);				\
	} else {							\
		parent = in;						\
		while (VRBT_LEFT(in, field))				\
			in = VRBT_LEFT(in, field);			\
		VRBT_SET_PARENT(child, in, field);			\
		VRBT_LEFT(in, field) = child;				\
		child = VRBT_RIGHT(in, field);				\
		if (parent != in) {					\
			VRBT_SET_PARENT(parent, in, field);		\
			VRBT_RIGHT(in, field) = parent;			\
			parent = VRBT_PARENT(in, field);			\
			VRBT_LEFT(parent, field) = child;			\
		}							\
		_VRBT_UP(in, field) = opar;				\
		opar = _VRBT_PTR(opar);					\
	}								\
	VRBT_SWAP_CHILD(head, opar, out, in, field);			\
	if (child != NULL)						\
		_VRBT_UP(child, field) = parent;				\
	if (parent != NULL) {						\
		opar = name##_VRBT_REMOVE_COLOR(head, parent, child);	\
		/* if rotation has made 'parent' the root of the same	\
		 * subtree as before, don't re-augment it. */		\
		if (parent == in && VRBT_LEFT(parent, field) == NULL) {	\
			opar = NULL;					\
			parent = VRBT_PARENT(parent, field);		\
		}							\
		_VRBT_AUGMENT_WALK(parent, opar, field);			\
		if (opar != NULL) {					\
			/*						\
			 * Elements rotated into the search path have	\
			 * changed subtrees, so update augmentation for	\
			 * them if AUGMENT_WALK didn't.			\
			 */						\
			(void)VRBT_AUGMENT_CHECK(opar);			\
			(void)VRBT_AUGMENT_CHECK(VRBT_PARENT(opar, field));	\
		}							\
	}								\
	return (out);							\
}

#define VRBT_GENERATE_INSERT_FINISH(name, type, field, attr)		\
/* Inserts a node into the RB tree */					\
attr struct type *							\
name##_VRBT_INSERT_FINISH(struct name *head, struct type *parent,		\
    struct type **pptr, struct type *elm)				\
{									\
	struct type *tmp = NULL;					\
									\
	VRBT_SET(elm, parent, field);					\
	*pptr = elm;							\
	if (parent != NULL)						\
		tmp = name##_VRBT_INSERT_COLOR(head, parent, elm);	\
	_VRBT_AUGMENT_WALK(elm, tmp, field);				\
	if (tmp != NULL)						\
		/*							\
		 * An element rotated into the search path has a	\
		 * changed subtree, so update augmentation for it if	\
		 * AUGMENT_WALK didn't.					\
		 */							\
		(void)VRBT_AUGMENT_CHECK(tmp);				\
	return (NULL);							\
}

#define VRBT_GENERATE_INSERT(name, type, field, cmp, attr)		\
/* Inserts a node into the RB tree */					\
attr struct type *							\
name##_VRBT_INSERT(struct name *head, struct type *elm)			\
{									\
	struct type *tmp;						\
	struct type **tmpp = &VRBT_ROOT(head);				\
	struct type *parent = NULL;					\
									\
	while ((tmp = *tmpp) != NULL) {					\
		parent = tmp;						\
		__typeof(cmp(NULL, NULL)) comp = (cmp)(elm, parent);	\
		if (comp < 0)						\
			tmpp = &VRBT_LEFT(parent, field);			\
		else if (comp > 0)					\
			tmpp = &VRBT_RIGHT(parent, field);		\
		else							\
			return (parent);				\
	}								\
	return (name##_VRBT_INSERT_FINISH(head, parent, tmpp, elm));	\
}

#define VRBT_GENERATE_FIND(name, type, field, cmp, attr)			\
/* Finds the node with the same key as elm */				\
attr struct type *							\
name##_VRBT_FIND(const struct name *head, const struct type *elm)			\
{									\
	struct type *tmp = VRBT_ROOT(head);				\
	__typeof(cmp(NULL, NULL)) comp;					\
	while (tmp) {							\
		comp = cmp(elm, tmp);					\
		if (comp < 0)						\
			tmp = VRBT_LEFT(tmp, field);			\
		else if (comp > 0)					\
			tmp = VRBT_RIGHT(tmp, field);			\
		else							\
			return (tmp);					\
	}								\
	return (NULL);							\
}

#define VRBT_GENERATE_NFIND(name, type, field, cmp, attr)			\
/* Finds the first node greater than or equal to the search key */	\
attr struct type *							\
name##_VRBT_NFIND(struct name *head, struct type *elm)			\
{									\
	struct type *tmp = VRBT_ROOT(head);				\
	struct type *res = NULL;					\
	__typeof(cmp(NULL, NULL)) comp;					\
	while (tmp) {							\
		comp = cmp(elm, tmp);					\
		if (comp < 0) {						\
			res = tmp;					\
			tmp = VRBT_LEFT(tmp, field);			\
		}							\
		else if (comp > 0)					\
			tmp = VRBT_RIGHT(tmp, field);			\
		else							\
			return (tmp);					\
	}								\
	return (res);							\
}

#define VRBT_GENERATE_NEXT(name, type, field, attr)			\
/* ARGSUSED */								\
attr struct type *							\
name##_VRBT_NEXT(struct type *elm)					\
{									\
	if (VRBT_RIGHT(elm, field)) {					\
		elm = VRBT_RIGHT(elm, field);				\
		while (VRBT_LEFT(elm, field))				\
			elm = VRBT_LEFT(elm, field);			\
	} else {							\
		while (VRBT_PARENT(elm, field) &&				\
		    (elm == VRBT_RIGHT(VRBT_PARENT(elm, field), field)))	\
			elm = VRBT_PARENT(elm, field);			\
		elm = VRBT_PARENT(elm, field);				\
	}								\
	return (elm);							\
}

#if defined(_KERNEL) && defined(DIAGNOSTIC)
#define _VRBT_ORDER_CHECK(cmp, lo, hi) do {				\
	KASSERT((cmp)(lo, hi) < 0, ("out of order insertion"));		\
} while (0)
#else
#define _VRBT_ORDER_CHECK(cmp, lo, hi) do {} while (0)
#endif

#define VRBT_GENERATE_INSERT_NEXT(name, type, field, cmp, attr)		\
/* Inserts a node into the next position in the RB tree */		\
attr struct type *							\
name##_VRBT_INSERT_NEXT(struct name *head,				\
    struct type *elm, struct type *next)				\
{									\
	struct type *tmp;						\
	struct type **tmpp = &VRBT_RIGHT(elm, field);			\
									\
	_VRBT_ORDER_CHECK(cmp, elm, next);				\
	if (name##_VRBT_NEXT(elm) != NULL)				\
		_VRBT_ORDER_CHECK(cmp, next, name##_VRBT_NEXT(elm));	\
	while ((tmp = *tmpp) != NULL) {					\
		elm = tmp;						\
		tmpp = &VRBT_LEFT(elm, field);				\
	}								\
	return (name##_VRBT_INSERT_FINISH(head, elm, tmpp, next));	\
}

#define VRBT_GENERATE_PREV(name, type, field, attr)			\
/* ARGSUSED */								\
attr struct type *							\
name##_VRBT_PREV(struct type *elm)					\
{									\
	if (VRBT_LEFT(elm, field)) {					\
		elm = VRBT_LEFT(elm, field);				\
		while (VRBT_RIGHT(elm, field))				\
			elm = VRBT_RIGHT(elm, field);			\
	} else {							\
		while (VRBT_PARENT(elm, field) &&				\
		    (elm == VRBT_LEFT(VRBT_PARENT(elm, field), field)))	\
			elm = VRBT_PARENT(elm, field);			\
		elm = VRBT_PARENT(elm, field);				\
	}								\
	return (elm);							\
}

#define VRBT_GENERATE_INSERT_PREV(name, type, field, cmp, attr)		\
/* Inserts a node into the prev position in the RB tree */		\
attr struct type *							\
name##_VRBT_INSERT_PREV(struct name *head,				\
    struct type *elm, struct type *prev)				\
{									\
	struct type *tmp;						\
	struct type **tmpp = &VRBT_LEFT(elm, field);			\
									\
	_VRBT_ORDER_CHECK(cmp, prev, elm);				\
	if (name##_VRBT_PREV(elm) != NULL)				\
		_VRBT_ORDER_CHECK(cmp, name##_VRBT_PREV(elm), prev);	\
	while ((tmp = *tmpp) != NULL) {					\
		elm = tmp;						\
		tmpp = &VRBT_RIGHT(elm, field);				\
	}								\
	return (name##_VRBT_INSERT_FINISH(head, elm, tmpp, prev));	\
}

#define VRBT_GENERATE_MINMAX(name, type, field, attr)			\
attr struct type *							\
name##_VRBT_MINMAX(const struct name *head, int val)				\
{									\
	struct type *tmp = VRBT_ROOT(head);				\
	struct type *parent = NULL;					\
	while (tmp) {							\
		parent = tmp;						\
		if (val < 0)						\
			tmp = VRBT_LEFT(tmp, field);			\
		else							\
			tmp = VRBT_RIGHT(tmp, field);			\
	}								\
	return (parent);						\
}

#define	VRBT_GENERATE_REINSERT(name, type, field, cmp, attr)		\
attr struct type *							\
name##_VRBT_REINSERT(struct name *head, struct type *elm)			\
{									\
	struct type *cmpelm;						\
	if (((cmpelm = VRBT_PREV(name, head, elm)) != NULL &&		\
	    cmp(cmpelm, elm) >= 0) ||					\
	    ((cmpelm = VRBT_NEXT(name, head, elm)) != NULL &&		\
	    cmp(elm, cmpelm) >= 0)) {					\
		/* XXXLAS: Remove/insert is heavy handed. */		\
		VRBT_REMOVE(name, head, elm);				\
		return (VRBT_INSERT(name, head, elm));			\
	}								\
	return (NULL);							\
}									\

#define VRBT_NEGINF	-1
#define VRBT_INF	1

#define VRBT_INSERT(name, x, y)	name##_VRBT_INSERT(x, y)
#define VRBT_INSERT_NEXT(name, x, y, z)	name##_VRBT_INSERT_NEXT(x, y, z)
#define VRBT_INSERT_PREV(name, x, y, z)	name##_VRBT_INSERT_PREV(x, y, z)
#define VRBT_REMOVE(name, x, y)	name##_VRBT_REMOVE(x, y)
#define VRBT_FIND(name, x, y)	name##_VRBT_FIND(x, y)
#define VRBT_NFIND(name, x, y)	name##_VRBT_NFIND(x, y)
#define VRBT_NEXT(name, x, y)	name##_VRBT_NEXT(y)
#define VRBT_PREV(name, x, y)	name##_VRBT_PREV(y)
#define VRBT_MIN(name, x)		name##_VRBT_MINMAX(x, VRBT_NEGINF)
#define VRBT_MAX(name, x)		name##_VRBT_MINMAX(x, VRBT_INF)
#define VRBT_REINSERT(name, x, y)	name##_VRBT_REINSERT(x, y)

#define VRBT_FOREACH(x, name, head)					\
	for ((x) = VRBT_MIN(name, head);					\
	     (x) != NULL;						\
	     (x) = name##_VRBT_NEXT(x))

#define VRBT_FOREACH_FROM(x, name, y)					\
	for ((x) = (y);							\
	    ((x) != NULL) && ((y) = name##_VRBT_NEXT(x), (x) != NULL);	\
	     (x) = (y))

#define VRBT_FOREACH_SAFE(x, name, head, y)				\
	for ((x) = VRBT_MIN(name, head);					\
	    ((x) != NULL) && ((y) = name##_VRBT_NEXT(x), (x) != NULL);	\
	     (x) = (y))

#define VRBT_FOREACH_REVERSE(x, name, head)				\
	for ((x) = VRBT_MAX(name, head);					\
	     (x) != NULL;						\
	     (x) = name##_VRBT_PREV(x))

#define VRBT_FOREACH_REVERSE_FROM(x, name, y)				\
	for ((x) = (y);							\
	    ((x) != NULL) && ((y) = name##_VRBT_PREV(x), (x) != NULL);	\
	     (x) = (y))

#define VRBT_FOREACH_REVERSE_SAFE(x, name, head, y)			\
	for ((x) = VRBT_MAX(name, head);					\
	    ((x) != NULL) && ((y) = name##_VRBT_PREV(x), (x) != NULL);	\
	     (x) = (y))

#endif	/* _VTREE_H_ */
