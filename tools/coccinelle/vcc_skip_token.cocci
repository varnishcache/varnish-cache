/*
 * This patch simplifies token parsing.
 */

@@
expression tl, tok;
@@

- ExpectErr(tl, tok);
- vcc_NextToken(tl);
+ SkipToken(tl, tok);
