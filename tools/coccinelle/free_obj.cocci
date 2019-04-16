/*
 * Make sure to use FREE_OBJ() instead of a plain free() to get additional
 * safeguards offered by the macro.
 */

@@
expression obj, objp, magic;
@@

(
ALLOC_OBJ(obj, magic);
|
CAST_OBJ(obj, objp, magic);
|
CAST_OBJ_NOTNULL(obj, objp, magic);
|
CHECK_OBJ(obj, magic);
|
CHECK_OBJ_NOTNULL(obj, magic);
|
CHECK_OBJ_ORNULL(obj, magic);
|
TAKE_OBJ_NOTNULL(obj, objp, magic);
)
...
- free(obj);
+ FREE_OBJ(obj);
