/*-
 * Copyright (c) 2011-2014 Varnish Software AS
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

#define P(x, ...)			\
	printf(x "\n", ##__VA_ARGS__)
#define VSC_LEVEL_F(v,l,e,d)		\
	printf("%s – %s\n\t%s\n\n", l, e, d);
#define VSC_F(n, t, l, f, v, e, d)	\
	printf("%s – %s (%s)\n\t%s\n\n", #n, e, VSC_level_##v, d);

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	P("================");
	P("varnish-counters");
	P("================");
	P("");

	P("---------------------------------");
	P("Varnish counter field definitions");
	P("---------------------------------");

	P(":Author: Tollef Fog Heen");
	P(":Date:   2011-09-20");
	P(":Version: 1.0");
	P(":Manual section: 7");
	P("");

	P("COUNTER LEVELS");
	P("==============");
	P("");
#include "tbl/vsc_levels.h"

	P("");
	P("MAIN COUNTERS");
	P("=============");
	P("");
#include "tbl/vsc_f_main.h"

	P("");
	P("LOCK COUNTERS");
	P("=============");
	P("");
#define VSC_DO_LCK
#include "tbl/vsc_fields.h"
#undef VSC_DO_LCK

	P("");
	P("PER MALLOC STORAGE COUNTERS");
	P("===========================");
	P("");
#define VSC_DO_SMA
#include "tbl/vsc_fields.h"
#undef  VSC_DO_SMA

	P("");
	P("PER FILE STORAGE COUNTERS");
	P("=========================");
	P("");
#define VSC_DO_SMF
#include "tbl/vsc_fields.h"
#undef VSC_DO_SMF

	P("");
	P("PER BACKEND COUNTERS");
	P("====================");
	P("");
#define VSC_DO_VBE
#include "tbl/vsc_fields.h"
#undef VSC_DO_VBE

	return (0);
}

