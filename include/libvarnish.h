/*
 * $Id$
 */

/* from libvarnish/argv.c */
void FreeArgv(char **argv);
char **ParseArgv(const char *s, int comment);

#ifdef CLOCK_MONOTONIC
/* from libvarnish/time.c */
void TIM_format(time_t t, char *p);
time_t TIM_parse(const char *p);
#endif

/* from libvarnish/version.c */
void varnish_version(const char *);

/* Assert zero return value */
#define AZ(foo)	do { assert((foo) == 0); } while (0)
