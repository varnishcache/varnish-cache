/*
 * $Id$
 */

#include <errno.h>
#include <time.h>

/* from libvarnish/argv.c */
void FreeArgv(char **argv);
char **ParseArgv(const char *s, int comment);

/* from libvarnish/time.c */
void TIM_format(time_t t, char *p);
time_t TIM_parse(const char *p);

/* from libvarnish/version.c */
void varnish_version(const char *);

/* from libvarnish/assert.c */
#ifdef WITHOUT_ASSERTS
#define assert(e)	((void)0)
#else /* WITH_ASSERTS */
#define assert(e)							\
do { 									\
	if (!(e))							\
		lbv_assert(__func__, __FILE__, __LINE__, #e, errno);	\
} while (0)
#endif

void lbv_assert(const char *, const char *, int, const char *, int);

/* Assert zero return value */
#define AZ(foo)	do { assert((foo) == 0); } while (0)
