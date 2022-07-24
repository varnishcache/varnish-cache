/*
 * This patch avoids printf formatting with VSLb
 */

@@
expression vsl, tag, str;
@@

- VSLb(vsl, tag, "%s", str)
+ VSLbs(vsl, tag, TOSTRAND(str))
