/*
 * Our code style inherited from FreeBSD explicitly requires brackets around a
 * function's return (value). The trick is to always add the parenthesis and
 * then undo the cases where we ended with two sets of them.
 *
 * It has the annoying effect of joining lines that were broken because of
 * the same code style and this needs to be fixed by hand.
 *
 * This does not apply to lib/libvgz/ as this is mostly bundled code and one
 * quick way to undo changes there after running this patch is:
 *
 *     git checkout -- lib/libvgz/
 *
 * Once libvgz is out of the way, one quick way to unbreak lines that had the
 * parenthesis in place is:
 *
 *     git checkout --patch
 *
 * An interactive prompt will ask what to do for each hunk of updated code and
 * replying 'y' to a false positive undoes the change on disk.
 */

@@
expression rv;
@@

- return rv;
+ return (rv);

@@
expression rv;
@@

- return ((rv));
+ return (rv);
