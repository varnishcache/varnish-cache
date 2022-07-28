/*
 * This patch removes a redundant null check and replaces assertions
 * on the magic with CHECK_OBJ()
 */

@@
expression obj, magic;
@@

if (obj != NULL) {
- CHECK_OBJ_NOTNULL(obj, magic);
+ CHECK_OBJ(obj, magic);
...
}

@@
expression obj, magicval;
@@

- assert(obj->magic == magicval);
+ CHECK_OBJ(obj, magicval);
