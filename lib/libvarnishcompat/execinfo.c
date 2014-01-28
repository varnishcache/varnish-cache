/*
 * Copyright (c) 2003 Maxim Sobolev <sobomax@FreeBSD.org>
 * All rights reserved.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"

#ifndef HAVE_BACKTRACE

#include "compat/execinfo.h"

#if defined (__GNUC__) && __GNUC__ >= 4	/* XXX Correct version to check for ? */

#include <sys/types.h>
#include <sys/uio.h>
#include <dlfcn.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void *getreturnaddr(int);
static void *getframeaddr(int);

int
backtrace(void **buffer, int size)
{
	int i;

	for (i = 1; getframeaddr(i + 1) != NULL && i != size + 1; i++) {
		buffer[i - 1] = getreturnaddr(i);
		if (buffer[i - 1] == NULL)
			break;
	}
	return (i - 1);
}

/*
 * XXX: This implementation should be changed to a much more conservative
 * XXX: memory strategy:  Allocate 4k up front, realloc 4K more as needed.
 */

char **
backtrace_symbols(void *const *buffer, int size)
{
	size_t clen, alen;
	int i;
	char **rval;

	clen = size * sizeof(char *);
	rval = malloc(clen);
	if (rval == NULL)
		return (NULL);
	for (i = 0; i < size; i++) {

#ifdef HAVE_DLADDR
		{
		Dl_info info;
		int offset;

		if (dladdr(buffer[i], &info) != 0) {
			if (info.dli_sname == NULL)
				info.dli_sname = "?";
			if (info.dli_saddr == NULL)
				info.dli_saddr = buffer[i];
			offset = (const char*)buffer[i] -
			    (const char*)info.dli_saddr;
			/* "0x01234567 <function+offset> at filename" */
			alen = 2 +                      /* "0x" */
			    (sizeof(void *) * 2) +   /* "01234567" */
			    2 +                      /* " <" */
			    strlen(info.dli_sname) + /* "function" */
			    1 +                      /* "+" */
			    10 +                     /* "offset */
			    5 +                      /* "> at " */
			    strlen(info.dli_fname) + /* "filename" */
			    1;                       /* "\0" */
			rval = realloc(rval, clen + alen);
			if (rval == NULL)
				return NULL;
			(void)snprintf((char *) rval + clen, alen,
			    "%p <%s+%d> at %s", buffer[i], info.dli_sname,
			    offset, info.dli_fname);
			rval[i] = (char *) clen;
			clen += alen;
			continue;
		}
		}
#endif
		alen = 2 +                      /* "0x" */
		    (sizeof(void *) * 2) +   /* "01234567" */
		    1;                       /* "\0" */
		rval = realloc(rval, clen + alen);
		if (rval == NULL)
			return NULL;
		(void)snprintf((char *) rval + clen, alen, "%p", buffer[i]);
		rval[i] = (char *) clen;
		clen += alen;
	}

	for (i = 0; i < size; i++)
		rval[i] += (long) rval;
	return (rval);
}

/* Binary expansion */
#define DO_P2_TIMES_1(x)	DO_P2_TIMES_0(x); DO_P2_TIMES_0((x) + (1<<0))
#define DO_P2_TIMES_2(x)	DO_P2_TIMES_1(x); DO_P2_TIMES_1((x) + (1<<1))
#define DO_P2_TIMES_3(x)	DO_P2_TIMES_2(x); DO_P2_TIMES_2((x) + (1<<2))
#define DO_P2_TIMES_4(x)	DO_P2_TIMES_3(x); DO_P2_TIMES_3((x) + (1<<3))
#define DO_P2_TIMES_5(x)	DO_P2_TIMES_4(x); DO_P2_TIMES_4((x) + (1<<4))
#define DO_P2_TIMES_6(x)	DO_P2_TIMES_5(x); DO_P2_TIMES_5((x) + (1<<5))
#define DO_P2_TIMES_7(x)	DO_P2_TIMES_6(x); DO_P2_TIMES_6((x) + (1<<6))

static void *
getreturnaddr(int level)
{

	switch(level) {
#define DO_P2_TIMES_0(x)  case (x): return __builtin_return_address((x) + 1)
	DO_P2_TIMES_7(0);
#undef DO_P2_TIMES_0
	default: return NULL;
	}
}

static void *
getframeaddr(int level)
{

	switch(level) {
#define DO_P2_TIMES_0(x)  case (x): return __builtin_frame_address((x) + 1)
	DO_P2_TIMES_7(0);
#undef DO_P2_TIMES_0
	default: return NULL;
	}
}

#else

int
backtrace(void **buffer, int size)
{
	(void)buffer;
	(void)size;
	return (0);
}

char **
backtrace_symbols(void *const *buffer, int size)
{
	(void)buffer;
	(void)size;
	return (0);
}

#endif /*  (__GNUC__) && __GNUC__ >= 4 */
#endif /* HAVE_BACKTRACE */
