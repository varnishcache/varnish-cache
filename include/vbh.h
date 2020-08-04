/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 * Binary Heap API (see: http://en.wikipedia.org/wiki/Binary_heap)
 *
 * XXX: doesn't scale back the array of pointers when items are deleted.
 */

/* Public Interface --------------------------------------------------*/

struct vbh;

typedef int vbh_cmp_t(void *priv, const void *a, const void *b);
	/*
	 * Comparison function.
	 * Should return true if item 'a' should be closer to the root
	 * than item 'b'
	 */

typedef void vbh_update_t(void *priv, void *a, unsigned newidx);
	/*
	 * Update function (optional)
	 * When items move in the tree, this function gets called to
	 * notify the item of its new index.
	 * Only needed if deleting non-root items.
	 */

struct vbh *VBH_new(void *priv, vbh_cmp_t, vbh_update_t);
	/*
	 * Create Binary tree
	 * 'priv' is passed to cmp and update functions.
	 */

void VBH_destroy(struct vbh **);
	/*
	 * Destroy an empty Binary tree
	 */

void VBH_insert(struct vbh *, void *);
	/*
	 * Insert an item
	 */

void VBH_reorder(const struct vbh *, unsigned idx);
	/*
	 * Move an order after changing its key value.
	 */

void VBH_delete(struct vbh *, unsigned idx);
	/*
	 * Delete an item
	 * The root item has 'idx' zero
	 */

void *VBH_root(const struct vbh *);
	/*
	 * Return the root item
	 */

#define VBH_NOIDX	0
