/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * Copyright (c) 1983, 1993 The Regents of the University of California.
 * All rights reserved.
 *
 * Author: Dag-Erling Smørgrav <des@des.no>
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
 * Partially from: $FreeBSD: head/lib/libc/stdlib/random.c 303342
 *
 */

#include "config.h"

#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vdef.h"

#include "vas.h"
#include "vrnd.h"


vrnd_lock_f *VRND_Lock;
vrnd_lock_f *VRND_Unlock;

/**********************************************************************
 * Stripped down random(3) implementation from FreeBSD, to provide
 * predicatable "random" numbers of testing purposes.
 */

#define	TYPE_3		3		/* x**31 + x**3 + 1 */
#define	DEG_3		31
#define	SEP_3		3

static uint32_t randtbl[DEG_3 + 1] = {
	TYPE_3,
	0x2cf41758, 0x27bb3711, 0x4916d4d1, 0x7b02f59f, 0x9b8e28eb, 0xc0e80269,
	0x696f5c16, 0x878f1ff5, 0x52d9c07f, 0x916a06cd, 0xb50b3a20, 0x2776970a,
	0xee4eb2a6, 0xe94640ec, 0xb1d65612, 0x9d1ed968, 0x1043f6b7, 0xa3432a76,
	0x17eacbb9, 0x3c09e2eb, 0x4f8c2b3,  0x708a1f57, 0xee341814, 0x95d0e4d2,
	0xb06f216c, 0x8bd2e72e, 0x8f7c38d7, 0xcfc6a8fc, 0x2a59495,  0xa20d2a69,
	0xe29d12d1
};

static uint32_t *fptr = &randtbl[SEP_3 + 1];
static uint32_t *rptr = &randtbl[1];

static uint32_t * const state = &randtbl[1];
static const int rand_deg = DEG_3;
static const int rand_sep = SEP_3;
static const uint32_t * const end_ptr = &randtbl[DEG_3 + 1];

static inline uint32_t
good_rand(uint32_t ctx)
{
/*
 * Compute x = (7^5 * x) mod (2^31 - 1)
 * wihout overflowing 31 bits:
 *      (2^31 - 1) = 127773 * (7^5) + 2836
 * From "Random number generators: good ones are hard to find",
 * Park and Miller, Communications of the ACM, vol. 31, no. 10,
 * October 1988, p. 1195.
 */
	int32_t hi, lo, x;

	/* Transform to [1, 0x7ffffffe] range. */
	x = (ctx % 0x7ffffffe) + 1;
	hi = x / 127773;
	lo = x % 127773;
	x = 16807 * lo - 2836 * hi;
	if (x < 0)
		x += 0x7fffffff;
	/* Transform to [0, 0x7ffffffd] range. */
	return (x - 1);
}

static long
vrnd_RandomTestable(void)
{
	uint32_t i;
	uint32_t *f, *r;

	/*
	 * Use local variables rather than static variables for speed.
	 */
	f = fptr; r = rptr;
	*f += *r;
	i = *f >> 1;	/* chucking least random bit */
	if (++f >= end_ptr) {
		f = state;
		++r;
	}
	else if (++r >= end_ptr) {
		r = state;
	}

	fptr = f; rptr = r;
	return ((long)i);
}


void
VRND_SeedTestable(unsigned int x)
{
	int i, lim;

	state[0] = (uint32_t)x;
	for (i = 1; i < rand_deg; i++)
		state[i] = good_rand(state[i - 1]);
	fptr = &state[rand_sep];
	rptr = &state[0];
	lim = 10 * rand_deg;
	for (i = 0; i < lim; i++)
		(void)vrnd_RandomTestable();
}

long
VRND_RandomTestable(void)
{
	long l;

	AN(VRND_Lock);
	VRND_Lock();
	l = vrnd_RandomTestable();
	AN(VRND_Unlock);
	VRND_Unlock();
	return (l);
}

double
VRND_RandomTestableDouble(void)
{
	return (
		ldexp((double)VRND_RandomTestable(), -31) +
		ldexp((double)VRND_RandomTestable(), -62)
	);
}

int
VRND_RandomCrypto(void *ptr, size_t len)
{
	int fd;
	char *p;
	ssize_t l;

	AN(ptr);
	fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0)
		return (-1);
	for (p = ptr; len > 0; len--, p++) {
		l = read(fd, p, 1);
		if (l != 1)
			break;
	}
	closefd(&fd);
	return (len == 0 ? 0 : -1);
}

void
VRND_SeedAll(void)
{
	unsigned long seed;

	AZ(VRND_RandomCrypto(&seed, sizeof seed));
	srandom(seed);
	AZ(VRND_RandomCrypto(&seed, sizeof seed));
	VRND_SeedTestable(seed);
	AZ(VRND_RandomCrypto(&seed, sizeof seed));
	srand48(seed);
}
