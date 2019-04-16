/*
 * This patch removes a redundant null check.
 */

@@
expression obj, magic;
@@

if (obj != NULL) {
- CHECK_OBJ_NOTNULL(obj, magic);
+ CHECK_OBJ(obj, magic);
...
}
