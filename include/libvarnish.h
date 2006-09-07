/*
 * $Id$
 */

#include <errno.h>
#include <time.h>
#include <stdint.h>

#ifndef NULL
#define NULL ((void*)0)
#endif

/* from libvarnish/argv.c */
void FreeArgv(char **argv);
char **ParseArgv(const char *s, int comment);

/* from libvarnish/crc32.c */
uint32_t crc32_2s(const char *p1, const char *p2);
uint32_t crc32_l(const void *p1, unsigned l);

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

#define LOCKSHM(foo)	AZ(pthread_mutex_lock(foo))
#define UNLOCKSHM(foo)	AZ(pthread_mutex_unlock(foo))

#if 1
#define LOCK(foo)	AZ(pthread_mutex_lock(foo))
#define UNLOCK(foo)	AZ(pthread_mutex_unlock(foo))
#else
#define LOCK(foo)	do { AZ(pthread_mutex_lock(foo)); VSL(SLT_Debug, 0, "LOCK(%s,%s,%d," #foo ")", __func__, __FILE__, __LINE__); } while (0);
#define UNLOCK(foo)	do { AZ(pthread_mutex_unlock(foo)); VSL(SLT_Debug, 0, "UNLOC(%s,%s,%d," #foo ")", __func__, __FILE__, __LINE__); } while (0);
#endif
