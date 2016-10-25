/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 * BAN control from VCL
 */

#include "config.h"

#include "vrt.h"
#include "vav.h"

#include "cache/cache.h"

#include "vcc_if.h"

/*--------------------------------------------------------------------*/

VCL_STRING __match_proto__(td_std_ban)
vmod_ban(VRT_CTX, VCL_STRING spec)
{
	char *a1, *a2, *a3;
	char **av;
	struct ban_proto *bp;
	const char *err = NULL;
	int i;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(ctx->vsl);
	AN(spec);

	bp = BAN_Build();
	if (bp == NULL) {
		err = "Out of Memory";
		VSLb(ctx->vsl, SLT_VCL_Error, "std.ban(): %s", err);
		return err;
	}
	av = VAV_Parse(spec, NULL, ARGV_NOESC);
	AN(av);
	if (av[0] != NULL) {
		err = WS_Copy(ctx->ws, av[0], -1);
		VSLb(ctx->vsl, SLT_VCL_Error, "std.ban(): %s", err);
		VAV_Free(av);
		BAN_Abandon(bp);
		return err;
	}
	for (i = 0; ;) {
		a1 = av[++i];
		if (a1 == NULL) {
			err = "No ban conditions found.";
			break;
		}
		a2 = av[++i];
		if (a2 == NULL) {
			err = "Expected comparison operator.";
			break;
		}
		a3 = av[++i];
		if (a3 == NULL) {
			err = "Expected second operand.";
			break;
		}
		err = BAN_AddTest(bp, a1, a2, a3);
		if (err) {
			err = WS_Copy(ctx->ws, err, -1);
			break;
		}
		if (av[++i] == NULL) {
			err = BAN_Commit(bp);
			if (err == NULL)
				bp = NULL;
			else
				err = WS_Copy(ctx->ws, err, -1);
			break;
		}
		if (strcmp(av[i], "&&")) {
			err = WS_Printf(ctx->ws,
			    "Expected && between conditions, found \"%s\"",
			    av[i]);
			break;
		}
	}
	if (bp != NULL)
		BAN_Abandon(bp);
	VAV_Free(av);
	if (err)
		VSLb(ctx->vsl, SLT_VCL_Error, "std.ban(): %s", err);
	return err ? err : "";
}
