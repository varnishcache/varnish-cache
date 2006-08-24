/*
 * $Id$
 */

#include <errno.h>
#include <time.h>

#ifndef NULL
#define NULL ((void*)0)
#endif

/* from libvarnish/argv.c */
void FreeArgv(char **argv);
char **ParseArgv(const char *s, int comment);

/* from libvarnish/time.c */
void TIM_format(time_t t, char *p);
time_t TIM_parse(const char *p);

/* from libvarnish/version.c */
void varnish_version(const char *);

/* from libvarnish/assert.c */

/*
 * assert(), AN() and AZ() are static checks that should not happen.
 * xxxassert(), XXXAN() and XXXAZ() are markers for missing code.
 */

#ifdef WITHOUT_ASSERTS
#define assert(e)	((void)0)
#else /* WITH_ASSERTS */
#define assert(e)							\
do { 									\
	if (!(e))							\
		lbv_assert(__func__, __FILE__, __LINE__, #e, errno);	\
} while (0)
#endif

#define xxxassert(e)							\
do { 									\
	if (!(e))							\
		lbv_xxxassert(__func__, __FILE__, __LINE__, #e, errno); \
} while (0)

void lbv_assert(const char *, const char *, int, const char *, int);
void lbv_xxxassert(const char *, const char *, int, const char *, int);

/* Assert zero return value */
#define AZ(foo)	do { assert((foo) == 0); } while (0)
#define AN(foo)	do { assert((foo) != NULL); } while (0)
#define XXXAZ(foo)	do { xxxassert((foo) == 0); } while (0)
#define XXXAN(foo)	do { xxxassert((foo) != NULL); } while (0)
