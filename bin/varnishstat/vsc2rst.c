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

#define P(x)			\
	printf(x "\n")
#define VSC_LEVEL_F(v,l,e,d)		\
	printf("%s – %s\n\t%s\n\n", l, e, d);
#define VSC_F(n, t, l, s, f, v, d, e)	\
	printf("%s – %s (%s)\n\t%s\n\n", #n, d, VSC_level_##v, e);

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	P("COUNTER LEVELS");
	P("==============");
	P("");
#include "tbl/vsc_levels.h"

	P("");
	P("MAIN COUNTERS (MAIN.*)");
	P("======================");
	P("");
#include "tbl/vsc_f_main.h"

	P("MANAGEMENT PROCESS COUNTERS (MGT.*)");
	P("===================================");
	P("");
#define VSC_DO_MGT
#include "tbl/vsc_fields.h"
#undef VSC_DO_MGT

	P("");
	P("PER MEMORY POOL COUNTERS (MEMPOOL.*)");
	P("====================================");
	P("");
#define VSC_DO_MEMPOOL
#include "tbl/vsc_fields.h"
#undef VSC_DO_MEMPOOL

	P("");
	P("PER MALLOC STORAGE COUNTERS (SMA.*)");
	P("===================================");
	P("");
#define VSC_DO_SMA
#include "tbl/vsc_fields.h"
#undef  VSC_DO_SMA

	P("");
	P("PER FILE STORAGE COUNTERS (SMF.*)");
	P("=================================");
	P("");
#define VSC_DO_SMF
#include "tbl/vsc_fields.h"
#undef VSC_DO_SMF

	P("");
	P("PER BACKEND COUNTERS (VBE.*)");
	P("============================");
	P("");
#define VSC_DO_VBE
#include "tbl/vsc_fields.h"
#undef VSC_DO_VBE

	P("");
	P("LOCK COUNTERS (LCK.*)");
	P("=====================");
	P("");
#define VSC_DO_LCK
#include "tbl/vsc_fields.h"
#undef VSC_DO_LCK

	return (0);
}
