/*	$NetBSD: tree.h,v 1.8 2004/03/28 19:38:30 provos Exp $	*/
/*	$OpenBSD: tree.h,v 1.7 2002/10/17 21:51:54 art Exp $	*/
/* $FreeBSD: release/9.0.0/sys/sys/tree.h 189204 2009-03-01 04:57:23Z bms $ */

/*-
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

/*
 * This file defines data structures for different types of trees:
 * splay trees and red-black trees.
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
 * A red-black tree is a binary search tree with the node color as an
 * extra attribute.  It fulfills a set of conditions:
 *	- every search path from the root to a leaf consists of the
 *	  same number of black nodes,
 *	- each red node (except for the root) has a black parent,
 *	- each leaf node is black.
 *
 * Every operation on a red-black tree is bounded as O(lg n).
 * The maximum height of a red-black tree is 2lg (n+1).
 */

#define VSPLAY_HEAD(name, type)						\
struct name {								\
	struct type *sph_root; /* root of the tree */			\
}

#define VSPLAY_INITIALIZER(root)					\
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
	VSPLAY_LEFT((head)->sph_root, field) = VSPLAY_RIGHT(tmp, field);\
	VSPLAY_RIGHT(tmp, field) = (head)->sph_root;			\
	(head)->sph_root = tmp;						\
} while (/*CONSTCOND*/ 0)

#define VSPLAY_ROTATE_LEFT(head, tmp, field) do {			\
	VSPLAY_RIGHT((head)->sph_root, field) = VSPLAY_LEFT(tmp, field);\
	VSPLAY_LEFT(tmp, field) = (head)->sph_root;			\
	(head)->sph_root = tmp;						\
} while (/*CONSTCOND*/ 0)

#define VSPLAY_LINKLEFT(head, tmp, field) do {				\
	VSPLAY_LEFT(tmp, field) = (head)->sph_root;			\
	tmp = (head)->sph_root;						\
	(head)->sph_root = VSPLAY_LEFT((head)->sph_root, field);	\
} while (/*CONSTCOND*/ 0)

#define VSPLAY_LINKRIGHT(head, tmp, field) do {				\
	VSPLAY_RIGHT(tmp, field) = (head)->sph_root;			\
	tmp = (head)->sph_root;						\
	(head)->sph_root = VSPLAY_RIGHT((head)->sph_root, field);	\
} while (/*CONSTCOND*/ 0)

#define VSPLAY_ASSEMBLE(head, node, left, right, field) do {		\
	VSPLAY_RIGHT(left, field) = VSPLAY_LEFT((head)->sph_root, field);\
	VSPLAY_LEFT(right, field) = VSPLAY_RIGHT((head)->sph_root, field);\
	VSPLAY_LEFT((head)->sph_root, field) = VSPLAY_RIGHT(node, field);\
	VSPLAY_RIGHT((head)->sph_root, field) = VSPLAY_LEFT(node, field);\
} while (/*CONSTCOND*/ 0)

/* Generates prototypes and inline functions */

#define VSPLAY_PROTOTYPE(name, type, field, cmp)			\
void name##_VSPLAY(struct name *, struct type *);			\
void name##_VSPLAY_MINMAX(struct name *, int);				\
struct type *name##_VSPLAY_INSERT(struct name *, struct type *);	\
struct type *name##_VSPLAY_REMOVE(struct name *, struct type *);	\
									\
/* Finds the node with the same key as elm */				\
static __inline struct type *						\
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
static __inline struct type *						\
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
static __inline struct type *						\
name##_VSPLAY_MIN_MAX(struct name *head, int val)			\
{									\
	name##_VSPLAY_MINMAX(head, val);				\
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
	    int __comp;							\
	    name##_VSPLAY(head, elm);					\
	    __comp = (cmp)(elm, (head)->sph_root);			\
	    if(__comp < 0) {						\
		    VSPLAY_LEFT(elm, field) = VSPLAY_LEFT((head)->sph_root, field);\
		    VSPLAY_RIGHT(elm, field) = (head)->sph_root;	\
		    VSPLAY_LEFT((head)->sph_root, field) = NULL;	\
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
	int __comp;							\
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
				VSPLAY_ROTATE_RIGHT(head, __tmp, field);\
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
				VSPLAY_ROTATE_RIGHT(head, __tmp, field);\
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

/* Macros that define a red-black tree */
#define VRB_HEAD(name, type)						\
struct name {								\
	struct type *rbh_root; /* root of the tree */			\
}

#define VRB_INITIALIZER(root)						\
	{ NULL }

#define VRB_INIT(root) do {						\
	(root)->rbh_root = NULL;					\
} while (/*CONSTCOND*/ 0)

#define VRB_BLACK	0
#define VRB_RED		1
#define VRB_ENTRY(type)							\
struct {								\
	struct type *rbe_left;		/* left element */		\
	struct type *rbe_right;		/* right element */		\
	struct type *rbe_parent;	/* parent element */		\
	int rbe_color;			/* node color */		\
}

#define VRB_LEFT(elm, field)		(elm)->field.rbe_left
#define VRB_RIGHT(elm, field)		(elm)->field.rbe_right
#define VRB_PARENT(elm, field)		(elm)->field.rbe_parent
#define VRB_COLOR(elm, field)		(elm)->field.rbe_color
#define VRB_ROOT(head)			(head)->rbh_root
#define VRB_EMPTY(head)			(VRB_ROOT(head) == NULL)

#define VRB_SET(elm, parent, field) do {				\
	VRB_PARENT(elm, field) = parent;				\
	VRB_LEFT(elm, field) = VRB_RIGHT(elm, field) = NULL;		\
	VRB_COLOR(elm, field) = VRB_RED;				\
} while (/*CONSTCOND*/ 0)

#define VRB_SET_BLACKRED(black, red, field) do {			\
	VRB_COLOR(black, field) = VRB_BLACK;				\
	VRB_COLOR(red, field) = VRB_RED;				\
} while (/*CONSTCOND*/ 0)

#ifndef VRB_AUGMENT
#define VRB_AUGMENT(x)	do {} while (0)
#endif

#define VRB_ROTATE_LEFT(head, elm, tmp, field) do {			\
	(tmp) = VRB_RIGHT(elm, field);					\
	if ((VRB_RIGHT(elm, field) = VRB_LEFT(tmp, field)) != NULL) {	\
		VRB_PARENT(VRB_LEFT(tmp, field), field) = (elm);	\
	}								\
	VRB_AUGMENT(elm);						\
	if ((VRB_PARENT(tmp, field) = VRB_PARENT(elm, field)) != NULL) {\
		if ((elm) == VRB_LEFT(VRB_PARENT(elm, field), field))	\
			VRB_LEFT(VRB_PARENT(elm, field), field) = (tmp);\
		else							\
			VRB_RIGHT(VRB_PARENT(elm, field), field) = (tmp);\
	} else								\
		(head)->rbh_root = (tmp);				\
	VRB_LEFT(tmp, field) = (elm);					\
	VRB_PARENT(elm, field) = (tmp);					\
	VRB_AUGMENT(tmp);						\
	if ((VRB_PARENT(tmp, field)))					\
		VRB_AUGMENT(VRB_PARENT(tmp, field));			\
} while (/*CONSTCOND*/ 0)

#define VRB_ROTATE_RIGHT(head, elm, tmp, field) do {			\
	(tmp) = VRB_LEFT(elm, field);					\
	if ((VRB_LEFT(elm, field) = VRB_RIGHT(tmp, field)) != NULL) {	\
		VRB_PARENT(VRB_RIGHT(tmp, field), field) = (elm);	\
	}								\
	VRB_AUGMENT(elm);						\
	if ((VRB_PARENT(tmp, field) = VRB_PARENT(elm, field)) != NULL) {\
		if ((elm) == VRB_LEFT(VRB_PARENT(elm, field), field))	\
			VRB_LEFT(VRB_PARENT(elm, field), field) = (tmp);\
		else							\
			VRB_RIGHT(VRB_PARENT(elm, field), field) = (tmp);\
	} else								\
		(head)->rbh_root = (tmp);				\
	VRB_RIGHT(tmp, field) = (elm);					\
	VRB_PARENT(elm, field) = (tmp);					\
	VRB_AUGMENT(tmp);						\
	if ((VRB_PARENT(tmp, field)))					\
		VRB_AUGMENT(VRB_PARENT(tmp, field));			\
} while (/*CONSTCOND*/ 0)

/* Generates prototypes and inline functions */
#define	VRB_PROTOTYPE(name, type, field, cmp)				\
	VRB_PROTOTYPE_INTERNAL(name, type, field, cmp,)
#define	VRB_PROTOTYPE_STATIC(name, type, field, cmp)			\
	VRB_PROTOTYPE_INTERNAL(name, type, field, cmp, __unused static)
#define VRB_PROTOTYPE_INTERNAL(name, type, field, cmp, attr)		\
/*lint -esym(528, name##_VRB_*) */					\
attr void name##_VRB_INSERT_COLOR(struct name *, struct type *);	\
attr void name##_VRB_REMOVE_COLOR(struct name *, struct type *, struct type *);\
attr struct type *name##_VRB_REMOVE(struct name *, struct type *);	\
attr struct type *name##_VRB_INSERT(struct name *, struct type *);	\
attr struct type *name##_VRB_FIND(const struct name *, const struct type *);	\
attr struct type *name##_VRB_NFIND(const struct name *, const struct type *);	\
attr struct type *name##_VRB_NEXT(struct type *);			\
attr struct type *name##_VRB_PREV(struct type *);			\
attr struct type *name##_VRB_MINMAX(const struct name *, int);		\
									\

/* Main rb operation.
 * Moves node close to the key of elm to top
 */
#define	VRB_GENERATE(name, type, field, cmp)				\
	VRB_GENERATE_INTERNAL(name, type, field, cmp,)
#define	VRB_GENERATE_STATIC(name, type, field, cmp)			\
	VRB_GENERATE_INTERNAL(name, type, field, cmp, __unused static)
#define VRB_GENERATE_INTERNAL(name, type, field, cmp, attr)		\
attr void								\
name##_VRB_INSERT_COLOR(struct name *head, struct type *elm)		\
{									\
	struct type *parent, *gparent, *tmp;				\
	while ((parent = VRB_PARENT(elm, field)) != NULL &&		\
	    VRB_COLOR(parent, field) == VRB_RED) {			\
		gparent = VRB_PARENT(parent, field);			\
		if (parent == VRB_LEFT(gparent, field)) {		\
			tmp = VRB_RIGHT(gparent, field);		\
			if (tmp && VRB_COLOR(tmp, field) == VRB_RED) {	\
				VRB_COLOR(tmp, field) = VRB_BLACK;	\
				VRB_SET_BLACKRED(parent, gparent, field);\
				elm = gparent;				\
				continue;				\
			}						\
			if (VRB_RIGHT(parent, field) == elm) {		\
				VRB_ROTATE_LEFT(head, parent, tmp, field);\
				tmp = parent;				\
				parent = elm;				\
				elm = tmp;				\
			}						\
			VRB_SET_BLACKRED(parent, gparent, field);	\
			VRB_ROTATE_RIGHT(head, gparent, tmp, field);	\
		} else {						\
			tmp = VRB_LEFT(gparent, field);			\
			if (tmp && VRB_COLOR(tmp, field) == VRB_RED) {	\
				VRB_COLOR(tmp, field) = VRB_BLACK;	\
				VRB_SET_BLACKRED(parent, gparent, field);\
				elm = gparent;				\
				continue;				\
			}						\
			if (VRB_LEFT(parent, field) == elm) {		\
				VRB_ROTATE_RIGHT(head, parent, tmp, field);\
				tmp = parent;				\
				parent = elm;				\
				elm = tmp;				\
			}						\
			VRB_SET_BLACKRED(parent, gparent, field);	\
			VRB_ROTATE_LEFT(head, gparent, tmp, field);	\
		}							\
	}								\
	VRB_COLOR(head->rbh_root, field) = VRB_BLACK;			\
}									\
									\
attr void								\
name##_VRB_REMOVE_COLOR(struct name *head, struct type *parent, struct type *elm) \
{									\
	struct type *tmp;						\
	while ((elm == NULL || VRB_COLOR(elm, field) == VRB_BLACK) &&	\
	    elm != VRB_ROOT(head)) {					\
		AN(parent);						\
		if (VRB_LEFT(parent, field) == elm) {			\
			tmp = VRB_RIGHT(parent, field);			\
			if (VRB_COLOR(tmp, field) == VRB_RED) {		\
				VRB_SET_BLACKRED(tmp, parent, field);	\
				VRB_ROTATE_LEFT(head, parent, tmp, field);\
				tmp = VRB_RIGHT(parent, field);		\
			}						\
			if ((VRB_LEFT(tmp, field) == NULL ||		\
			    VRB_COLOR(VRB_LEFT(tmp, field), field) == VRB_BLACK) &&\
			    (VRB_RIGHT(tmp, field) == NULL ||		\
			    VRB_COLOR(VRB_RIGHT(tmp, field), field) == VRB_BLACK)) {\
				VRB_COLOR(tmp, field) = VRB_RED;	\
				elm = parent;				\
				parent = VRB_PARENT(elm, field);	\
			} else {					\
				if (VRB_RIGHT(tmp, field) == NULL ||	\
				    VRB_COLOR(VRB_RIGHT(tmp, field), field) == VRB_BLACK) {\
					struct type *oleft;		\
					if ((oleft = VRB_LEFT(tmp, field)) \
					    != NULL)			\
						VRB_COLOR(oleft, field) = VRB_BLACK;\
					VRB_COLOR(tmp, field) = VRB_RED;\
					VRB_ROTATE_RIGHT(head, tmp, oleft, field);\
					tmp = VRB_RIGHT(parent, field);	\
				}					\
				VRB_COLOR(tmp, field) = VRB_COLOR(parent, field);\
				VRB_COLOR(parent, field) = VRB_BLACK;	\
				if (VRB_RIGHT(tmp, field))		\
					VRB_COLOR(VRB_RIGHT(tmp, field), field) = VRB_BLACK;\
				VRB_ROTATE_LEFT(head, parent, tmp, field);\
				elm = VRB_ROOT(head);			\
				break;					\
			}						\
		} else {						\
			tmp = VRB_LEFT(parent, field);			\
			if (VRB_COLOR(tmp, field) == VRB_RED) {		\
				VRB_SET_BLACKRED(tmp, parent, field);	\
				VRB_ROTATE_RIGHT(head, parent, tmp, field);\
				tmp = VRB_LEFT(parent, field);		\
			}						\
			if ((VRB_LEFT(tmp, field) == NULL ||		\
			    VRB_COLOR(VRB_LEFT(tmp, field), field) == VRB_BLACK) &&\
			    (VRB_RIGHT(tmp, field) == NULL ||		\
			    VRB_COLOR(VRB_RIGHT(tmp, field), field) == VRB_BLACK)) {\
				VRB_COLOR(tmp, field) = VRB_RED;	\
				elm = parent;				\
				parent = VRB_PARENT(elm, field);	\
			} else {					\
				if (VRB_LEFT(tmp, field) == NULL ||	\
				    VRB_COLOR(VRB_LEFT(tmp, field), field) == VRB_BLACK) {\
					struct type *oright;		\
					if ((oright = VRB_RIGHT(tmp, field)) \
					    != NULL)			\
						VRB_COLOR(oright, field) = VRB_BLACK;\
					VRB_COLOR(tmp, field) = VRB_RED;\
					VRB_ROTATE_LEFT(head, tmp, oright, field);\
					tmp = VRB_LEFT(parent, field);	\
				}					\
				VRB_COLOR(tmp, field) = VRB_COLOR(parent, field);\
				VRB_COLOR(parent, field) = VRB_BLACK;	\
				if (VRB_LEFT(tmp, field))		\
					VRB_COLOR(VRB_LEFT(tmp, field), field) = VRB_BLACK;\
				VRB_ROTATE_RIGHT(head, parent, tmp, field);\
				elm = VRB_ROOT(head);			\
				break;					\
			}						\
		}							\
	}								\
	if (elm)							\
		VRB_COLOR(elm, field) = VRB_BLACK;			\
}									\
									\
attr struct type *							\
name##_VRB_REMOVE(struct name *head, struct type *elm)			\
{									\
	struct type *child, *parent, *old = elm;			\
	int color;							\
	if (VRB_LEFT(elm, field) == NULL)				\
		child = VRB_RIGHT(elm, field);				\
	else if (VRB_RIGHT(elm, field) == NULL)				\
		child = VRB_LEFT(elm, field);				\
	else {								\
		struct type *left;					\
		elm = VRB_RIGHT(elm, field);				\
		while ((left = VRB_LEFT(elm, field)) != NULL)		\
			elm = left;					\
		child = VRB_RIGHT(elm, field);				\
		parent = VRB_PARENT(elm, field);			\
		color = VRB_COLOR(elm, field);				\
		if (child)						\
			VRB_PARENT(child, field) = parent;		\
		if (parent) {						\
			if (VRB_LEFT(parent, field) == elm)		\
				VRB_LEFT(parent, field) = child;	\
			else						\
				VRB_RIGHT(parent, field) = child;	\
			VRB_AUGMENT(parent);				\
		} else							\
			VRB_ROOT(head) = child;				\
		if (VRB_PARENT(elm, field) == old)			\
			parent = elm;					\
		(elm)->field = (old)->field;				\
		if (VRB_PARENT(old, field)) {				\
			if (VRB_LEFT(VRB_PARENT(old, field), field) == old)\
				VRB_LEFT(VRB_PARENT(old, field), field) = elm;\
			else						\
				VRB_RIGHT(VRB_PARENT(old, field), field) = elm;\
			VRB_AUGMENT(VRB_PARENT(old, field));		\
		} else							\
			VRB_ROOT(head) = elm;				\
		VRB_PARENT(VRB_LEFT(old, field), field) = elm;		\
		if (VRB_RIGHT(old, field))				\
			VRB_PARENT(VRB_RIGHT(old, field), field) = elm;	\
		if (parent) {						\
			left = parent;					\
			do {						\
				VRB_AUGMENT(left);			\
			} while ((left = VRB_PARENT(left, field)) != NULL); \
		}							\
		goto color;						\
	}								\
	parent = VRB_PARENT(elm, field);				\
	color = VRB_COLOR(elm, field);					\
	if (child)							\
		VRB_PARENT(child, field) = parent;			\
	if (parent) {							\
		if (VRB_LEFT(parent, field) == elm)			\
			VRB_LEFT(parent, field) = child;		\
		else							\
			VRB_RIGHT(parent, field) = child;		\
		VRB_AUGMENT(parent);					\
	} else								\
		VRB_ROOT(head) = child;					\
color:									\
	if (color == VRB_BLACK) {					\
		name##_VRB_REMOVE_COLOR(head, parent, child);		\
	}								\
	return (old);							\
}									\
									\
/* Inserts a node into the RB tree */					\
attr struct type *							\
name##_VRB_INSERT(struct name *head, struct type *elm)			\
{									\
	struct type *tmp;						\
	struct type *parent = NULL;					\
	int comp = 0;							\
	tmp = VRB_ROOT(head);						\
	while (tmp) {							\
		parent = tmp;						\
		comp = (cmp)(elm, parent);				\
		if (comp < 0)						\
			tmp = VRB_LEFT(tmp, field);			\
		else if (comp > 0)					\
			tmp = VRB_RIGHT(tmp, field);			\
		else							\
			return (tmp);					\
	}								\
	VRB_SET(elm, parent, field);					\
	if (parent != NULL) {						\
		if (comp < 0)						\
			VRB_LEFT(parent, field) = elm;			\
		else							\
			VRB_RIGHT(parent, field) = elm;			\
		VRB_AUGMENT(parent);					\
	} else								\
		VRB_ROOT(head) = elm;					\
	name##_VRB_INSERT_COLOR(head, elm);				\
	return (NULL);							\
}									\
									\
/* Finds the node with the same key as elm */				\
attr struct type *							\
name##_VRB_FIND(const struct name *head, const struct type *elm)	\
{									\
	struct type *tmp = VRB_ROOT(head);				\
	int comp;							\
	while (tmp) {							\
		comp = cmp(elm, tmp);					\
		if (comp < 0)						\
			tmp = VRB_LEFT(tmp, field);			\
		else if (comp > 0)					\
			tmp = VRB_RIGHT(tmp, field);			\
		else							\
			return (tmp);					\
	}								\
	return (NULL);							\
}									\
									\
/* Finds the first node greater than or equal to the search key */	\
attr struct type *							\
name##_VRB_NFIND(const struct name *head, const struct type *elm)	\
{									\
	struct type *tmp = VRB_ROOT(head);				\
	struct type *res = NULL;					\
	int comp;							\
	while (tmp) {							\
		comp = cmp(elm, tmp);					\
		if (comp < 0) {						\
			res = tmp;					\
			tmp = VRB_LEFT(tmp, field);			\
		}							\
		else if (comp > 0)					\
			tmp = VRB_RIGHT(tmp, field);			\
		else							\
			return (tmp);					\
	}								\
	return (res);							\
}									\
									\
/* ARGSUSED */								\
attr struct type *							\
name##_VRB_NEXT(struct type *elm)					\
{									\
	if (VRB_RIGHT(elm, field)) {					\
		elm = VRB_RIGHT(elm, field);				\
		while (VRB_LEFT(elm, field))				\
			elm = VRB_LEFT(elm, field);			\
	} else {							\
		if (VRB_PARENT(elm, field) &&				\
		    (elm == VRB_LEFT(VRB_PARENT(elm, field), field)))	\
			elm = VRB_PARENT(elm, field);			\
		else {							\
			while (VRB_PARENT(elm, field) &&		\
			    (elm == VRB_RIGHT(VRB_PARENT(elm, field), field)))\
				elm = VRB_PARENT(elm, field);		\
			elm = VRB_PARENT(elm, field);			\
		}							\
	}								\
	return (elm);							\
}									\
									\
/* ARGSUSED */								\
attr struct type *							\
name##_VRB_PREV(struct type *elm)					\
{									\
	if (VRB_LEFT(elm, field)) {					\
		elm = VRB_LEFT(elm, field);				\
		while (VRB_RIGHT(elm, field))				\
			elm = VRB_RIGHT(elm, field);			\
	} else {							\
		if (VRB_PARENT(elm, field) &&				\
		    (elm == VRB_RIGHT(VRB_PARENT(elm, field), field)))	\
			elm = VRB_PARENT(elm, field);			\
		else {							\
			while (VRB_PARENT(elm, field) &&		\
			    (elm == VRB_LEFT(VRB_PARENT(elm, field), field)))\
				elm = VRB_PARENT(elm, field);		\
			elm = VRB_PARENT(elm, field);			\
		}							\
	}								\
	return (elm);							\
}									\
									\
attr struct type *							\
name##_VRB_MINMAX(const struct name *head, int val)			\
{									\
	struct type *tmp = VRB_ROOT(head);				\
	struct type *parent = NULL;					\
	while (tmp) {							\
		parent = tmp;						\
		if (val < 0)						\
			tmp = VRB_LEFT(tmp, field);			\
		else							\
			tmp = VRB_RIGHT(tmp, field);			\
	}								\
	return (parent);						\
}

#define VRB_NEGINF	-1
#define VRB_INF	1

#define VRB_INSERT(name, x, y)	name##_VRB_INSERT(x, y)
#define VRB_REMOVE(name, x, y)	name##_VRB_REMOVE(x, y)
#define VRB_FIND(name, x, y)	name##_VRB_FIND(x, y)
#define VRB_NFIND(name, x, y)	name##_VRB_NFIND(x, y)
#define VRB_NEXT(name, x, y)	name##_VRB_NEXT(y)
#define VRB_PREV(name, x, y)	name##_VRB_PREV(y)
#define VRB_MIN(name, x)		name##_VRB_MINMAX(x, VRB_NEGINF)
#define VRB_MAX(name, x)		name##_VRB_MINMAX(x, VRB_INF)

#define VRB_FOREACH(x, name, head)					\
	for ((x) = VRB_MIN(name, head);					\
	     (x) != NULL;						\
	     (x) = name##_VRB_NEXT(x))

#define VRB_FOREACH_FROM(x, name, y)					\
	for ((x) = (y);							\
	    ((x) != NULL) && ((y) = name##_VRB_NEXT(x), (x) != NULL);	\
	     (x) = (y))

#define VRB_FOREACH_SAFE(x, name, head, y)				\
	for ((x) = VRB_MIN(name, head);					\
	    ((x) != NULL) && ((y) = name##_VRB_NEXT(x), (x) != NULL);	\
	     (x) = (y))

#define VRB_FOREACH_REVERSE(x, name, head)				\
	for ((x) = VRB_MAX(name, head);					\
	     (x) != NULL;						\
	     (x) = name##_VRB_PREV(x))

#define VRB_FOREACH_REVERSE_FROM(x, name, y)				\
	for ((x) = (y);							\
	    ((x) != NULL) && ((y) = name##_VRB_PREV(x), (x) != NULL);	\
	     (x) = (y))

#define VRB_FOREACH_REVERSE_SAFE(x, name, head, y)			\
	for ((x) = VRB_MAX(name, head);					\
	    ((x) != NULL) && ((y) = name##_VRB_PREV(x), (x) != NULL);	\
	     (x) = (y))

#endif	/* _VTREE_H_ */
