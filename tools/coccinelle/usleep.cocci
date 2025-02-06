/* This patch turns hard-coded usleep() usage into a VTIM_sleep() equivalent.
 */

@found@
constant int usec;
position pos;
@@

usleep@pos(usec)

@script:python conv@
usec << found.usec;
pos << found.pos;
sec;
@@

coccinelle.sec = cocci.make_expr("{:g}".format(int(usec) / 1000000.0))

@replace@
constant int found.usec;
position found.pos;
expression conv.sec;
@@

- usleep@pos(usec)
+ VTIM_sleep(sec)

@uncast@
expression e;
@@

- (void)
  VTIM_sleep(e)
