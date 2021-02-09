/*
 * facilitate second half of phks vdp signature overhaul
 */

@@
expression req;
@@

-VDP_Close(req)
+VDP_Close(req->vdc)

@@
expression req;
@@

-VDP_DeliverObj(req)
+VDP_DeliverObj(req->vdc, req->objcore)

@@
expression req, vdp, priv;
@@

-VDP_Push(req, vdp, priv)
+VDP_Push(req->vdc, req->ws, vdp, priv)

@@
expression req, vdp, priv;
@@

-VDP_Push(req, &vdp, priv)
+VDP_Push(req->vdc, req->ws, &vdp, priv)
