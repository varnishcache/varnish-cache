/*
 * This patch removes a redundant null check and replaces assertions
 * on the magic with CHECK_OBJ()
 */

@@
expression obj, magicval;
@@

if (obj != NULL) {
- CHECK_OBJ_NOTNULL(obj, magicval);
+ CHECK_OBJ(obj, magicval);
...
}

@@
expression obj, magicval;
@@

- assert(obj->magic == magicval);
+ CHECK_OBJ(obj, magicval);
