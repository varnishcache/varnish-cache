/*
 * This patch fixes the order of VSB_tofile arguments.
 */

@@
idexpression struct vsb *vsb;
idexpression int fd;
@@

- VSB_tofile(fd, vsb)
+ VSB_tofile(vsb, fd)

@@
idexpression struct vsb[] vsb;
idexpression int fd;
@@

- VSB_tofile(fd, vsb)
+ VSB_tofile(vsb, fd)

@@
idexpression struct vsb *vsb;
expression fd;
@@

- VSB_tofile(fd, vsb)
+ VSB_tofile(vsb, fd)

/* Opportunistic fallback */

@@
idexpression int fd;
expression vsb;
@@

- VSB_tofile(fd, vsb)
+ VSB_tofile(vsb, fd)

/* Opportunistic last resort */

@@
expression fd, other;
@@

- VSB_tofile(fd, other->vsb)
+ VSB_tofile(other->vsb, fd)
