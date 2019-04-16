/*
 * The closefd() macro takes a pointer to the file descriptor to wipe it in
 * addition to closing the file and checking there was no error. This can
 * prevent use-after-close bugs. It will not work if the variable or field
 * holding the file descriptor has a const qualifier. It also make sures that
 * fd looks like a valid file descriptor before even calling close().
 *
 * The second part of the patch undoes the change when fd is a constant such
 * as STDIN_FILENO where it becomes nonsensical.
 */

@@
expression fd;
@@

- AZ(close(fd));
+ closefd(&fd);

@@
constant fd;
@@

- closefd(&fd);
+ AZ(close(fd));
