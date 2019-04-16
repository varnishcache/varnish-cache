/*
 * Patch to change code with respect to #2969: Replacement of
 * WS_Reserve(ws, sz) by WS_ReserveAll(ws) / WS_ReserveSize(ws, sz)
 *
 * NOTE this patch does not check/fix error handling:
 * - WS_ReserveAll(ws) : Always needs WS_Release(ws, sz)
 * - WS_ReserveSize(ws, sz): needs WS_Release(ws, sz) if retval != 0
 */
@@
expression ws;
identifier ptr;
@@

-ptr = WS_Reserve(ws, 0);
+ptr = WS_ReserveAll(ws);

@@
expression ws, sz;
identifier ptr;
@@

-ptr = WS_Reserve(ws, sz);
+ptr = WS_ReserveSize(ws, sz);
