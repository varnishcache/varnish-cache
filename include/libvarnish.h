/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2007 Linpro AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
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
#define AN(foo)	do { assert((foo) != 0); } while (0)
#define XXXAZ(foo)	do { xxxassert((foo) == 0); } while (0)
#define XXXAN(foo)	do { xxxassert((foo) != 0); } while (0)
