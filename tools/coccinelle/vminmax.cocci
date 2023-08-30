/*
 * This patch refactors v{min,max}_t to v{min,max} where types agree
 *
 * Note: Has false positives on pointer types, tolerated for clarity
 */
using "varnish.iso"

@@ type T; T e1, e2; @@

- vmin_t(T, e1, e2)
+ vmin(e1, e2)

@@ type T; T e1, e2; @@

- vmax_t(T, e1, e2)
+ vmax(e1, e2)
