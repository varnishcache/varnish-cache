/*
 * This patch simplifies code using the REPLACE() macro.
 */

@@
expression ptr, val;
@@

- free(ptr);
- ptr = strdup(val);
- AN(ptr);
+ REPLACE(ptr, val);
