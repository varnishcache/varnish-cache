/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 */

struct parspec;

typedef void tweak_t(struct cli *, const struct parspec *, const char *arg);

struct parspec {
	const char	*name;
	tweak_t		*func;
	volatile void	*priv;
	double		min;
	double		max;
	const char	*descr;
	int		 flags;
#define DELAYED_EFFECT	(1<<0)
#define EXPERIMENTAL	(1<<1)
#define MUST_RESTART	(1<<2)
#define MUST_RELOAD	(1<<3)
#define WIZARD		(1<<4)
	const char	*def;
	const char	*units;
};

void tweak_generic_uint(struct cli *cli,
    volatile unsigned *dest, const char *arg, unsigned min, unsigned max);
void tweak_uint(struct cli *cli, const struct parspec *par, const char *arg);
void tweak_timeout(struct cli *cli,
    const struct parspec *par, const char *arg);

extern struct params master;

/* mgt_pool.c */
extern const struct parspec WRK_parspec[];
