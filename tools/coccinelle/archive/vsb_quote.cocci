/*
 * This patch applies the update of the VSB_quote[_pfx] signature
 *
 * note: declaring the parameters just as expression is simplistic,
 * but does the job for this one-off patch
 */

@@
expression vsb, pfx, ptr, len, how;
@@

- VSB_quote_pfx(vsb, pfx, ptr, len, how)
+ AZ(VSB_quote_pfx(vsb, pfx, ptr, len, how, NULL))

@@
expression vsb, ptr, len, how;
@@

- VSB_quote(vsb, ptr, len, how)
+ AZ(VSB_quote(vsb, ptr, len, how))

