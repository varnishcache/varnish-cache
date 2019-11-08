/*
 * This patch removes useless calls to printf-type functions.
 */

@@
expression vsb, fmt;
@@

- VSB_printf(vsb, fmt);
+ VSB_cat(vsb, fmt);
