/*
 * Provide an API to avoid direct next token access.
 */

@@
struct vcc *tl;
expression e;
@@

-e = VTAILQ_NEXT(tl->t, list);
+e = vcc_PeekToken(tl);
+ERRCHK(tl);

@@
struct token *t;
expression e;
@@

-e = VTAILQ_NEXT(t, list);
+e = vcc_PeekTokenFrom(tl, t);
+ERRCHK(tl);

@@
struct vcc *tl;
@@

-VTAILQ_NEXT(tl->t, list)
+TODO_vcc_PeekToken_with_ERRCHK(tl)

@@
struct token *t;
@@

-VTAILQ_NEXT(t, list)
+TODO_vcc_Peek_with_ERRCHK(tl, t)
