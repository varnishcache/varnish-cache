/*
 * $Id$
 *
 * Binary Heap API (see: http://en.wikipedia.org/wiki/Binary_heap)
 *
 * XXX: doesn't scale back the array of pointers when items are deleted.
 */

/* Public Interface --------------------------------------------------*/

struct binheap;

typedef int binheap_cmp_t(void *priv, void *a, void *b);
	/*
	 * Comparison function.
 	 * Should return true if item 'a' should be closer to the root
	 * than item 'b'
	 */

typedef void binheap_update_t(void *priv, void *a, unsigned newidx);
	/*
	 * Update function (optional)
	 * When items move in the tree, this function gets called to
	 * notify the item of its new index.
	 * Only needed if deleting non-root items.
	 */

struct binheap *binheap_new(void *priv, binheap_cmp_t, binheap_update_t);
	/*
	 * Create Binary tree
	 * 'priv' is passed to cmp and update functions.
 	 */

void binheap_insert(struct binheap *, void *);
	/*
	 * Insert an item
	 */

void binheap_delete(struct binheap *, unsigned idx);
	/*
	 * Delete an item
	 * The root item has 'idx' zero
	 */

void *binheap_root(struct binheap *);
	/*
	 * Return the root item
	 */

