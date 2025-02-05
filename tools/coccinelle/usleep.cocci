/* This patch turns hard-coded usleep() usage into a VTIM_sleep() equivalent.
 *
 * Unfortunately, if a file contains the same usleep() call twice, each call
 * site will be substituted twice.
 *
 * This code:
 *
 *     usleep(10000);
 *     usleep(10000);
 *
 * Will turn into this:
 *
 *     VTIM_sleep(0.01)VTIM_sleep(0.01);
 *     VTIM_sleep(0.01)VTIM_sleep(0.01);
 *
 * Fortunately, it does not compile and cannot go unnoticed.
 */

@found@
constant int usec;
@@

usleep(usec)

@script:python conv@
usec << found.usec;
sec;
@@

coccinelle.sec = cocci.make_expr("{:g}".format(int(usec) / 1000000.0))

@uncast@
constant int found.usec;
@@

- (void)usleep(usec)
+ usleep(usec)

@replace@
constant int found.usec;
expression conv.sec;
@@

-  usleep(usec)
++ VTIM_sleep(sec)
