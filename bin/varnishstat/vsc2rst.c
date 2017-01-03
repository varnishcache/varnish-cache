/*-
 * Copyright (c) 2011-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Tollef Fog Heen <tfheen@varnish-software.com>
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

#include "config.h"

#include <stdio.h>

#define VSC_LEVEL_F(v,l,e,d)		\
	static const char VSC_level_##v[] = l;
#include "tbl/vsc_levels.h"
#undef VSC_LEVEL_F

static void
L(const char *s)
{
	printf("\n%s\n", s);
	for (;*s != '\0'; s++)
		putchar('=');
	putchar('\n');
	putchar('\n');
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	L("COUNTER LEVELS");

#define VSC_LEVEL_F(v,l,e,d)		\
	printf("%s – %s\n\t%s\n\n", l, e, d);

#include "tbl/vsc_levels.h"

#define VSC_DO(U,l,t,h) L(h);

#define VSC_F(n, t, l, s, f, v, d, e)	\
	printf("%s – %s (%s)\n\t%s\n\n", #n, d, VSC_level_##v, e);

#define VSC_DONE(U,l,t)

#include "tbl/vsc_all.h"
	return (0);
}
