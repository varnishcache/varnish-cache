/*
 * $Id$
 */

/* from libvarnish/argv.c */
void FreeArgv(char **argv);
char **ParseArgv(const char *s, int comment);


/* Assert zero return value */
#define AZ(foo)	do { assert((foo) == 0); } while (0)
