/*
 * The TAKE_OBJ_NOTNULL() macro emulates move semantics and better conveys the
 * intent behind a common pattern in the code base, usually before freeing an
 * object.
 *
 * This may fail to capture other incarnations of this pattern where the order
 * of the operations is not exactly followed so we try several combinations.
 */

@@
expression obj, objp, magic;
@@

- AN(objp);
...
- obj = *objp;
...
- *objp = NULL;
...
- CHECK_OBJ_NOTNULL(obj, magic);
+ TAKE_OBJ_NOTNULL(obj, objp, magic);

@@
expression obj, objp, magic;
@@

- AN(*objp);
...
- obj = *objp;
...
- *objp = NULL;
...
- CHECK_OBJ_NOTNULL(obj, magic);
+ TAKE_OBJ_NOTNULL(obj, objp, magic);

@@
expression obj, objp, magic;
@@

- AN(objp);
...
- obj = *objp;
...
- CHECK_OBJ_NOTNULL(obj, magic);
...
- *objp = NULL;
+ TAKE_OBJ_NOTNULL(obj, objp, magic);

@@
expression obj, objp, magic;
@@

- AN(objp);
...
- obj = *objp;
...
- CHECK_OBJ_NOTNULL(obj, magic);
+ TAKE_OBJ_NOTNULL(obj, objp, magic);
...
- *objp = NULL;

@@
expression obj, objp, magic;
@@

- obj = *objp;
...
- *objp = NULL;
...
- CHECK_OBJ_NOTNULL(obj, magic);
+ TAKE_OBJ_NOTNULL(obj, objp, magic);

@@
expression obj, priv, magic;
@@

- CAST_OBJ_NOTNULL(obj, *priv, magic);
- *priv = NULL;
+ TAKE_OBJ_NOTNULL(obj, priv, magic);

@@
expression obj, priv, magic;
@@

- CAST_OBJ_NOTNULL(obj, priv, magic);
- priv = NULL;
+ TAKE_OBJ_NOTNULL(obj, &priv, magic);
