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
	    if (__comp < 0) {						\
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
#define VRBT_HEAD(name, type)						\
struct name {								\
	struct type *rbh_root; /* root of the tree */			\
}

#define VRBT_INITIALIZER(root)						\
	{ NULL }

#define VRBT_INIT(root) do {						\
	(root)->rbh_root = NULL;					\
} while (/*CONSTCOND*/ 0)

#define VRBT_BLACK	0
#define VRBT_RED		1
#define VRBT_ENTRY(type)						\
struct {								\
	struct type *rbe_left;		/* left element */		\
	struct type *rbe_right;		/* right element */		\
	struct type *rbe_parent;	/* parent element */		\
	int rbe_color;			/* node color */		\
}

#define VRBT_LEFT(elm, field)		(elm)->field.rbe_left
#define VRBT_RIGHT(elm, field)		(elm)->field.rbe_right
#define VRBT_PARENT(elm, field)		(elm)->field.rbe_parent
#define VRBT_COLOR(elm, field)		(elm)->field.rbe_color
#define VRBT_ROOT(head)			(head)->rbh_root
#define VRBT_EMPTY(head)		(VRBT_ROOT(head) == NULL)

#define VRBT_SET(elm, parent, field) do {				\
	VRBT_PARENT(elm, field) = parent;				\
	VRBT_LEFT(elm, field) = VRBT_RIGHT(elm, field) = NULL;		\
	VRBT_COLOR(elm, field) = VRBT_RED;				\
} while (/*CONSTCOND*/ 0)

#define VRBT_SET_BLACKRED(black, red, field) do {			\
	VRBT_COLOR(black, field) = VRBT_BLACK;				\
	VRBT_COLOR(red, field) = VRBT_RED;				\
} while (/*CONSTCOND*/ 0)

#ifndef VRBT_AUGMENT
#define VRBT_AUGMENT(x)	do {} while (0)
#endif

#define VRBT_ROTATE_LEFT(head, elm, tmp, field) do {			\
	(tmp) = VRBT_RIGHT(elm, field);					\
	if ((VRBT_RIGHT(elm, field) = VRBT_LEFT(tmp, field)) != NULL) {	\
		VRBT_PARENT(VRBT_LEFT(tmp, field), field) = (elm);	\
	}								\
	VRBT_AUGMENT(elm);						\
	if ((VRBT_PARENT(tmp, field) = VRBT_PARENT(elm, field)) != NULL) {\
		if ((elm) == VRBT_LEFT(VRBT_PARENT(elm, field), field))	\
			VRBT_LEFT(VRBT_PARENT(elm, field), field) = (tmp);\
		else							\
			VRBT_RIGHT(VRBT_PARENT(elm, field), field) = (tmp);\
	} else								\
		(head)->rbh_root = (tmp);				\
	VRBT_LEFT(tmp, field) = (elm);					\
	VRBT_PARENT(elm, field) = (tmp);				\
	VRBT_AUGMENT(tmp);						\
	if ((VRBT_PARENT(tmp, field)))					\
		VRBT_AUGMENT(VRBT_PARENT(tmp, field));			\
} while (/*CONSTCOND*/ 0)

#define VRBT_ROTATE_RIGHT(head, elm, tmp, field) do {			\
	(tmp) = VRBT_LEFT(elm, field);					\
	if ((VRBT_LEFT(elm, field) = VRBT_RIGHT(tmp, field)) != NULL) {	\
		VRBT_PARENT(VRBT_RIGHT(tmp, field), field) = (elm);	\
	}								\
	VRBT_AUGMENT(elm);						\
	if ((VRBT_PARENT(tmp, field) = VRBT_PARENT(elm, field)) != NULL) {\
		if ((elm) == VRBT_LEFT(VRBT_PARENT(elm, field), field))	\
			VRBT_LEFT(VRBT_PARENT(elm, field), field) = (tmp);\
		else							\
			VRBT_RIGHT(VRBT_PARENT(elm, field), field) = (tmp);\
	} else								\
		(head)->rbh_root = (tmp);				\
	VRBT_RIGHT(tmp, field) = (elm);					\
	VRBT_PARENT(elm, field) = (tmp);				\
	VRBT_AUGMENT(tmp);						\
	if ((VRBT_PARENT(tmp, field)))					\
		VRBT_AUGMENT(VRBT_PARENT(tmp, field));			\
} while (/*CONSTCOND*/ 0)

/* Generates prototypes and inline functions */
#define	VRBT_PROTOTYPE(name, type, field, cmp)			\
	VRBT_PROTOTYPE_INTERNAL(name, type, field, cmp,)
#define	VRBT_PROTOTYPE_STATIC(name, type, field, cmp)		\
	VRBT_PROTOTYPE_INTERNAL(name, type, field, cmp, v_unused_ static)
#define VRBT_PROTOTYPE_INTERNAL(name, type, field, cmp, attr)		\
/*lint -esym(528, name##_VRBT_*) */					\
attr void name##_VRBT_INSERT_COLOR(struct name *, struct type *);	\
attr void name##_VRBT_REMOVE_COLOR(struct name *, struct type *, struct type *);\
attr struct type *name##_VRBT_REMOVE(struct name *, struct type *);	\
attr struct type *name##_VRBT_INSERT(struct name *, struct type *);	\
attr struct type *name##_VRBT_FIND(const struct name *, const struct type *);	\
attr struct type *name##_VRBT_NFIND(const struct name *, const struct type *);	\
attr struct type *name##_VRBT_NEXT(struct type *);			\
attr struct type *name##_VRBT_PREV(struct type *);			\
attr struct type *name##_VRBT_MINMAX(const struct name *, int);		\
									\

/* Main rb operation.
 * Moves node close to the key of elm to top
 */
#define	VRBT_GENERATE(name, type, field, cmp)			\
	VRBT_GENERATE_INTERNAL(name, type, field, cmp,)
#define	VRBT_GENERATE_STATIC(name, type, field, cmp)		\
	VRBT_GENERATE_INTERNAL(name, type, field, cmp, v_unused_ static)
#define VRBT_GENERATE_INTERNAL(name, type, field, cmp, attr)		\
attr void								\
name##_VRBT_INSERT_COLOR(struct name *head, struct type *elm)		\
{									\
	struct type *parent, *gparent, *tmp;				\
	while ((parent = VRBT_PARENT(elm, field)) != NULL &&		\
	    VRBT_COLOR(parent, field) == VRBT_RED) {			\
		gparent = VRBT_PARENT(parent, field);			\
		if (parent == VRBT_LEFT(gparent, field)) {		\
			tmp = VRBT_RIGHT(gparent, field);		\
			if (tmp && VRBT_COLOR(tmp, field) == VRBT_RED) {\
				VRBT_COLOR(tmp, field) = VRBT_BLACK;	\
				VRBT_SET_BLACKRED(parent, gparent, field);\
				elm = gparent;				\
				continue;				\
			}						\
			if (VRBT_RIGHT(parent, field) == elm) {	\
				VRBT_ROTATE_LEFT(head, parent, tmp, field);\
				tmp = parent;				\
				parent = elm;				\
				elm = tmp;				\
			}						\
			VRBT_SET_BLACKRED(parent, gparent, field);	\
			VRBT_ROTATE_RIGHT(head, gparent, tmp, field);	\
		} else {						\
			tmp = VRBT_LEFT(gparent, field);		\
			if (tmp && VRBT_COLOR(tmp, field) == VRBT_RED) {\
				VRBT_COLOR(tmp, field) = VRBT_BLACK;	\
				VRBT_SET_BLACKRED(parent, gparent, field);\
				elm = gparent;				\
				continue;				\
			}						\
			if (VRBT_LEFT(parent, field) == elm) {		\
				VRBT_ROTATE_RIGHT(head, parent, tmp, field);\
				tmp = parent;				\
				parent = elm;				\
				elm = tmp;				\
			}						\
			VRBT_SET_BLACKRED(parent, gparent, field);	\
			VRBT_ROTATE_LEFT(head, gparent, tmp, field);	\
		}							\
	}								\
	VRBT_COLOR(head->rbh_root, field) = VRBT_BLACK;			\
}									\
									\
attr void								\
name##_VRBT_REMOVE_COLOR(struct name *head, struct type *parent, struct type *elm) \
{									\
	struct type *tmp;						\
	while ((elm == NULL || VRBT_COLOR(elm, field) == VRBT_BLACK) &&	\
	    elm != VRBT_ROOT(head)) {					\
		AN(parent);						\
		if (VRBT_LEFT(parent, field) == elm) {			\
			tmp = VRBT_RIGHT(parent, field);		\
			if (VRBT_COLOR(tmp, field) == VRBT_RED) {	\
				VRBT_SET_BLACKRED(tmp, parent, field);	\
				VRBT_ROTATE_LEFT(head, parent, tmp, field);\
				tmp = VRBT_RIGHT(parent, field);	\
			}						\
			if ((VRBT_LEFT(tmp, field) == NULL ||		\
			    VRBT_COLOR(VRBT_LEFT(tmp, field), field) == VRBT_BLACK) &&\
			    (VRBT_RIGHT(tmp, field) == NULL ||		\
			    VRBT_COLOR(VRBT_RIGHT(tmp, field), field) == VRBT_BLACK)) {\
				VRBT_COLOR(tmp, field) = VRBT_RED;	\
				elm = parent;				\
				parent = VRBT_PARENT(elm, field);	\
			} else {					\
				if (VRBT_RIGHT(tmp, field) == NULL ||	\
				    VRBT_COLOR(VRBT_RIGHT(tmp, field), field) == VRBT_BLACK) {\
					struct type *oleft;		\
					if ((oleft = VRBT_LEFT(tmp, field)) \
					    != NULL)			\
						VRBT_COLOR(oleft, field) = VRBT_BLACK;\
					VRBT_COLOR(tmp, field) = VRBT_RED;\
					VRBT_ROTATE_RIGHT(head, tmp, oleft, field);\
					tmp = VRBT_RIGHT(parent, field);\
				}					\
				VRBT_COLOR(tmp, field) = VRBT_COLOR(parent, field);\
				VRBT_COLOR(parent, field) = VRBT_BLACK;	\
				if (VRBT_RIGHT(tmp, field))		\
					VRBT_COLOR(VRBT_RIGHT(tmp, field), field) = VRBT_BLACK;\
				VRBT_ROTATE_LEFT(head, parent, tmp, field);\
				elm = VRBT_ROOT(head);			\
				break;					\
			}						\
		} else {						\
			tmp = VRBT_LEFT(parent, field);			\
			if (VRBT_COLOR(tmp, field) == VRBT_RED) {	\
				VRBT_SET_BLACKRED(tmp, parent, field);	\
				VRBT_ROTATE_RIGHT(head, parent, tmp, field);\
				tmp = VRBT_LEFT(parent, field);		\
			}						\
			if ((VRBT_LEFT(tmp, field) == NULL ||		\
			    VRBT_COLOR(VRBT_LEFT(tmp, field), field) == VRBT_BLACK) &&\
			    (VRBT_RIGHT(tmp, field) == NULL ||		\
			    VRBT_COLOR(VRBT_RIGHT(tmp, field), field) == VRBT_BLACK)) {\
				VRBT_COLOR(tmp, field) = VRBT_RED;	\
				elm = parent;				\
				parent = VRBT_PARENT(elm, field);	\
			} else {					\
				if (VRBT_LEFT(tmp, field) == NULL ||	\
				    VRBT_COLOR(VRBT_LEFT(tmp, field), field) == VRBT_BLACK) {\
					struct type *oright;		\
					if ((oright = VRBT_RIGHT(tmp, field)) \
					    != NULL)			\
						VRBT_COLOR(oright, field) = VRBT_BLACK;\
					VRBT_COLOR(tmp, field) = VRBT_RED;\
					VRBT_ROTATE_LEFT(head, tmp, oright, field);\
					tmp = VRBT_LEFT(parent, field);	\
				}					\
				VRBT_COLOR(tmp, field) = VRBT_COLOR(parent, field);\
				VRBT_COLOR(parent, field) = VRBT_BLACK;	\
				if (VRBT_LEFT(tmp, field))		\
					VRBT_COLOR(VRBT_LEFT(tmp, field), field) = VRBT_BLACK;\
				VRBT_ROTATE_RIGHT(head, parent, tmp, field);\
				elm = VRBT_ROOT(head);			\
				break;					\
			}						\
		}							\
	}								\
	if (elm)							\
		VRBT_COLOR(elm, field) = VRBT_BLACK;			\
}									\
									\
attr struct type *							\
name##_VRBT_REMOVE(struct name *head, struct type *elm)			\
{									\
	struct type *child, *parent, *old = elm;			\
	int color;							\
	if (VRBT_LEFT(elm, field) == NULL)				\
		child = VRBT_RIGHT(elm, field);				\
	else if (VRBT_RIGHT(elm, field) == NULL)			\
		child = VRBT_LEFT(elm, field);				\
	else {								\
		struct type *left;					\
		elm = VRBT_RIGHT(elm, field);				\
		while ((left = VRBT_LEFT(elm, field)) != NULL)		\
			elm = left;					\
		child = VRBT_RIGHT(elm, field);				\
		parent = VRBT_PARENT(elm, field);			\
		color = VRBT_COLOR(elm, field);				\
		if (child)						\
			VRBT_PARENT(child, field) = parent;		\
		if (parent) {						\
			if (VRBT_LEFT(parent, field) == elm)		\
				VRBT_LEFT(parent, field) = child;	\
			else						\
				VRBT_RIGHT(parent, field) = child;	\
			VRBT_AUGMENT(parent);				\
		} else							\
			VRBT_ROOT(head) = child;			\
		if (VRBT_PARENT(elm, field) == old)			\
			parent = elm;					\
		(elm)->field = (old)->field;				\
		if (VRBT_PARENT(old, field)) {				\
			if (VRBT_LEFT(VRBT_PARENT(old, field), field) == old)\
				VRBT_LEFT(VRBT_PARENT(old, field), field) = elm;\
			else						\
				VRBT_RIGHT(VRBT_PARENT(old, field), field) = elm;\
			VRBT_AUGMENT(VRBT_PARENT(old, field));		\
		} else							\
			VRBT_ROOT(head) = elm;				\
		VRBT_PARENT(VRBT_LEFT(old, field), field) = elm;	\
		if (VRBT_RIGHT(old, field))				\
			VRBT_PARENT(VRBT_RIGHT(old, field), field) = elm;\
		if (parent) {						\
			left = parent;					\
			do {						\
				VRBT_AUGMENT(left);			\
			} while ((left = VRBT_PARENT(left, field)) != NULL);\
		}							\
		goto color;						\
	}								\
	parent = VRBT_PARENT(elm, field);				\
	color = VRBT_COLOR(elm, field);					\
	if (child)							\
		VRBT_PARENT(child, field) = parent;			\
	if (parent) {							\
		if (VRBT_LEFT(parent, field) == elm)			\
			VRBT_LEFT(parent, field) = child;		\
		else							\
			VRBT_RIGHT(parent, field) = child;		\
		VRBT_AUGMENT(parent);					\
	} else								\
		VRBT_ROOT(head) = child;				\
color:									\
	if (color == VRBT_BLACK) {					\
		name##_VRBT_REMOVE_COLOR(head, parent, child);		\
	}								\
	return (old);							\
}									\
									\
/* Inserts a node into the RB tree */					\
attr struct type *							\
name##_VRBT_INSERT(struct name *head, struct type *elm)			\
{									\
	struct type *tmp;						\
	struct type *parent = NULL;					\
	int comp = 0;							\
	tmp = VRBT_ROOT(head);						\
	while (tmp) {							\
		parent = tmp;						\
		comp = (cmp)(elm, parent);				\
		if (comp < 0)						\
			tmp = VRBT_LEFT(tmp, field);			\
		else if (comp > 0)					\
			tmp = VRBT_RIGHT(tmp, field);			\
		else							\
			return (tmp);					\
	}								\
	VRBT_SET(elm, parent, field);					\
	if (parent != NULL) {						\
		if (comp < 0)						\
			VRBT_LEFT(parent, field) = elm;			\
		else							\
			VRBT_RIGHT(parent, field) = elm;		\
		VRBT_AUGMENT(parent);					\
	} else								\
		VRBT_ROOT(head) = elm;					\
	name##_VRBT_INSERT_COLOR(head, elm);				\
	return (NULL);							\
}									\
									\
/* Finds the node with the same key as elm */				\
attr struct type *							\
name##_VRBT_FIND(const struct name *head, const struct type *elm)	\
{									\
	struct type *tmp = VRBT_ROOT(head);				\
	int comp;							\
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
}									\
									\
/* Finds the first node greater than or equal to the search key */	\
attr struct type *							\
name##_VRBT_NFIND(const struct name *head, const struct type *elm)	\
{									\
	struct type *tmp = VRBT_ROOT(head);				\
	struct type *res = NULL;					\
	int comp;							\
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
}									\
									\
/* ARGSUSED */								\
attr struct type *							\
name##_VRBT_NEXT(struct type *elm)					\
{									\
	if (VRBT_RIGHT(elm, field)) {					\
		elm = VRBT_RIGHT(elm, field);				\
		while (VRBT_LEFT(elm, field))				\
			elm = VRBT_LEFT(elm, field);			\
	} else {							\
		if (VRBT_PARENT(elm, field) &&				\
		    (elm == VRBT_LEFT(VRBT_PARENT(elm, field), field)))	\
			elm = VRBT_PARENT(elm, field);			\
		else {							\
			while (VRBT_PARENT(elm, field) &&		\
			    (elm == VRBT_RIGHT(VRBT_PARENT(elm, field), field)))\
				elm = VRBT_PARENT(elm, field);		\
			elm = VRBT_PARENT(elm, field);			\
		}							\
	}								\
	return (elm);							\
}									\
									\
/* ARGSUSED */								\
attr struct type *							\
name##_VRBT_PREV(struct type *elm)					\
{									\
	if (VRBT_LEFT(elm, field)) {					\
		elm = VRBT_LEFT(elm, field);				\
		while (VRBT_RIGHT(elm, field))				\
			elm = VRBT_RIGHT(elm, field);			\
	} else {							\
		if (VRBT_PARENT(elm, field) &&				\
		    (elm == VRBT_RIGHT(VRBT_PARENT(elm, field), field)))\
			elm = VRBT_PARENT(elm, field);			\
		else {							\
			while (VRBT_PARENT(elm, field) &&		\
			    (elm == VRBT_LEFT(VRBT_PARENT(elm, field), field)))\
				elm = VRBT_PARENT(elm, field);		\
			elm = VRBT_PARENT(elm, field);			\
		}							\
	}								\
	return (elm);							\
}									\
									\
attr struct type *							\
name##_VRBT_MINMAX(const struct name *head, int val)			\
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

#define VRBT_NEGINF	-1
#define VRBT_INF	1

#define VRBT_INSERT(name, x, y)	name##_VRBT_INSERT(x, y)
#define VRBT_REMOVE(name, x, y)	name##_VRBT_REMOVE(x, y)
#define VRBT_FIND(name, x, y)	name##_VRBT_FIND(x, y)
#define VRBT_NFIND(name, x, y)	name##_VRBT_NFIND(x, y)
#define VRBT_NEXT(name, x, y)	name##_VRBT_NEXT(y)
#define VRBT_PREV(name, x, y)	name##_VRBT_PREV(y)
#define VRBT_MIN(name, x)	name##_VRBT_MINMAX(x, VRBT_NEGINF)
#define VRBT_MAX(name, x)	name##_VRBT_MINMAX(x, VRBT_INF)

#define VRBT_FOREACH(x, name, head)					\
	for ((x) = VRBT_MIN(name, head);				\
	     (x) != NULL;						\
	     (x) = name##_VRBT_NEXT(x))

#define VRBT_FOREACH_FROM(x, name, y)					\
	for ((x) = (y);							\
	    ((x) != NULL) && ((y) = name##_VRBT_NEXT(x), (x) != NULL);	\
	     (x) = (y))

#define VRBT_FOREACH_SAFE(x, name, head, y)				\
	for ((x) = VRBT_MIN(name, head);				\
	    ((x) != NULL) && ((y) = name##_VRBT_NEXT(x), (x) != NULL);	\
	     (x) = (y))

#define VRBT_FOREACH_REVERSE(x, name, head)				\
	for ((x) = VRBT_MAX(name, head);				\
	     (x) != NULL;						\
	     (x) = name##_VRBT_PREV(x))

#define VRBT_FOREACH_REVERSE_FROM(x, name, y)				\
	for ((x) = (y);							\
	    ((x) != NULL) && ((y) = name##_VRBT_PREV(x), (x) != NULL);	\
	     (x) = (y))

#define VRBT_FOREACH_REVERSE_SAFE(x, name, head, y)			\
	for ((x) = VRBT_MAX(name, head);				\
	    ((x) != NULL) && ((y) = name##_VRBT_PREV(x), (x) != NULL);	\
	     (x) = (y))

#endif	/* _VTREE_H_ */
