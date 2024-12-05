@@
identifier p, elem;
expression lim, arr;
iterator name VARRAY_FOREACH;
iterator name VPTRS_ITER;
@@

- VARRAY_FOREACH(elem, arr, lim) {
...
-   p = *elem;
+ VPTRS_ITER(p, arr, lim) {
...
}
