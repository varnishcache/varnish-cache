/*-
 * Copyright (c) 2021 Varnish Software
 * All rights reserved.
 *
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Simple tail queue implementation
 *
 */

#ifndef VSTQ_H_INCLUDED
#define VSTQ_H_INCLUDED

#define VSTQ_HEAD(name, type)						\
	struct name {							\
		struct type *stq_first;					\
		struct type *stq_last;					\
	}

#define VSTQ_HEAD_INITIALIZER						\
	{ NULL, NULL }

#define VSTQ_ENTRY(type)						\
	struct {							\
		struct type *stq_next;					\
		struct type *stq_prev;					\
	}

#define VSTQ_INIT(head)							\
	do {								\
		(head)->stq_first = (head)->stq_last = NULL;		\
	} while (0)

#define VSTQ_EMPTY(head)		((head)->stq_first == NULL)

#define VSTQ_FIRST(head)		((head)->stq_first)

#define VSTQ_LAST(head)		((head)->stq_last)

#define VSTQ_NEXT(e, f)		((e)->f.stq_next)

#define VSTQ_PREV(e, f)		((e)->f.stq_prev)

#define VSTQ_FOREACH(var, head, f)					\
	for ((var) = VSTQ_FIRST((head));				\
	     (var);							\
	     (var) = VSTQ_NEXT((var), f))

#define VSTQ_FOREACH_SAFE(var, head, f, tvar)				\
	for ((var) = VSTQ_FIRST((head));				\
	     (var) && ((tvar) = VSTQ_NEXT((var), f), 1);		\
	     (var) = (tvar))

#define VSTQ_FOREACH_REVERSE(var, head, f)				\
	for ((var) = VSTQ_LAST((head));					\
	     (var);							\
	     (var) = VSTQ_PREV((var), f))

#define VSTQ_INSERT_BEFORE(head, le, e, f)				\
	do {								\
		if ((le)->f.stq_prev == NULL)				\
			VSTQ_INSERT_HEAD(head, e, f);			\
		else {							\
			(e)->f.stq_next = le;				\
			(e)->f.stq_prev = (le)->f.stq_prev;		\
			(e)->f.stq_next->f.stq_prev = e;		\
			(e)->f.stq_prev->f.stq_next = e;		\
		}							\
	} while (0)

#define VSTQ_INSERT_AFTER(head, le, e, f)				\
	do {								\
		if ((le)->f.stq_next == NULL)				\
			VSTQ_INSERT_TAIL(head, e, f);			\
		else {							\
			(e)->f.stq_prev = le;				\
			(e)->f.stq_next = (le)->f.stq_next;		\
			(e)->f.stq_prev->f.stq_next = e;		\
			(e)->f.stq_next->f.stq_prev = e;		\
		}							\
	} while (0)

#define VSTQ_INSERT_HEAD(head, e, f)					\
	do {								\
		(e)->f.stq_prev = NULL;					\
		(e)->f.stq_next = (head)->stq_first;			\
		if ((e)->f.stq_next)					\
			(e)->f.stq_next->f.stq_prev = e;		\
		if ((head)->stq_last == NULL)				\
			(head)->stq_last = e;				\
		(head)->stq_first = e;					\
	} while (0)

#define VSTQ_INSERT_TAIL(head, e, f)					\
	do {								\
		(e)->f.stq_next = NULL;					\
		(e)->f.stq_prev = (head)->stq_last;			\
		if ((e)->f.stq_prev)					\
			(e)->f.stq_prev->f.stq_next = e;		\
		if ((head)->stq_first == NULL)				\
			(head)->stq_first = e;				\
		(head)->stq_last = e;					\
	} while (0)

#define VSTQ_REMOVE(head, e, f)						\
	do {								\
		if ((e)->f.stq_prev == NULL && (e)->f.stq_next == NULL) { \
			(head)->stq_first = (head)->stq_last = NULL;	\
		} else if ((e)->f.stq_prev == NULL) {			\
			(e)->f.stq_next->f.stq_prev = NULL;		\
			(head)->stq_first = (e)->f.stq_next;		\
		} else if ((e)->f.stq_next == NULL) {			\
			(e)->f.stq_prev->f.stq_next = NULL;		\
			(head)->stq_last = (e)->f.stq_prev;		\
		} else {						\
			(e)->f.stq_prev->f.stq_next = (e)->f.stq_next;	\
			(e)->f.stq_next->f.stq_prev = (e)->f.stq_prev;	\
		}							\
		(e)->f.stq_prev = (e)->f.stq_next = NULL;		\
	} while (0)

#endif
