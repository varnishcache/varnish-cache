/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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

#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>

#include <netdb.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"
#include "libvarnish.h"

/*--------------------------------------------------------------------
 * Parse directors
 */

void
vcc_ParseRandomDirector(struct tokenlist *tl, const struct token *t_policy,
    const struct token *t_dir)
{
	struct token *t_field, *t_be;
	int nbh, nelem;
	struct fld_spec *fs, *mfs;
	unsigned u, retries;
	const char *first;

	fs = vcc_FldSpec(tl, "?retries", NULL);

	retries = 0;
	while (tl->t->tok != '{') {
		vcc_IsField(tl, &t_field, fs);
		ERRCHK(tl);
		if (vcc_IdIs(t_field, "retries")) {
			ExpectErr(tl, CNUM);
			retries = vcc_UintVal(tl);
			ERRCHK(tl);
			vcc_NextToken(tl);
			ExpectErr(tl, ';');
			vcc_NextToken(tl);
		} else {
			ErrInternal(tl);
		}
	}

	mfs = vcc_FldSpec(tl, "!backend", "!weight", NULL);

	Fc(tl, 0,
	    "\nstatic const struct vrt_dir_random_entry vdre_%.*s[] = {\n",
	    PF(t_dir));

	for (nelem = 0; tl->t->tok != '}'; nelem++) {	/* List of members */
		first = "";
		t_be = tl->t;
		vcc_ResetFldSpec(mfs);
		nbh = -1;

		ExpectErr(tl, '{');
		vcc_NextToken(tl);
		Fc(tl, 0, "\t{");

		while (tl->t->tok != '}') {	/* Member fields */
			vcc_IsField(tl, &t_field, mfs);
			ERRCHK(tl);
			if (vcc_IdIs(t_field, "backend")) {
				vcc_ParseBackendHost(tl, &nbh,
				    t_dir, t_policy, nelem);
				Fc(tl, 0, "%s .host = &bh_%d", first, nbh);
				ERRCHK(tl);
			} else if (vcc_IdIs(t_field, "weight")) {
				ExpectErr(tl, CNUM);
				u = vcc_UintVal(tl);
				ERRCHK(tl);
				if (u == 0) {
					vsb_printf(tl->sb,
					    "The .weight must be higher "
					    "than zero.");
					vcc_ErrToken(tl, tl->t);
					vsb_printf(tl->sb, " at\n");
					vcc_ErrWhere(tl, tl->t);
					return;
				}
				Fc(tl, 0, "%s .weight = %u", first, u);
				vcc_NextToken(tl);
				ExpectErr(tl, ';');
				vcc_NextToken(tl);
			} else {
				ErrInternal(tl);
			}
			first = ", ";
		}
		vcc_FieldsOk(tl, mfs);
		if (tl->err) {
			vsb_printf(tl->sb,
			    "\nIn member host specification starting at:\n");
			vcc_ErrWhere(tl, t_be);
			return;
		}
		Fc(tl, 0, " },\n");
		vcc_NextToken(tl);
	}
	Fc(tl, 0, "};\n");
	Fc(tl, 0,
	    "\nstatic const struct vrt_dir_random vdr_%.*s = {\n",
	    PF(t_dir));
	Fc(tl, 0, "\t.name = \"%.*s\",\n", PF(t_dir));
	Fc(tl, 0, "\t.retries = %u,\n", retries);
	Fc(tl, 0, "\t.nmember = %d,\n", nelem);
	Fc(tl, 0, "\t.members = vdre_%.*s,\n", PF(t_dir));
	Fc(tl, 0, "};\n");
	Fi(tl, 0,
	    "\tVRT_init_dir_random(cli, &VGC_backend_%.*s , &vdr_%.*s);\n",
	    PF(t_dir), PF(t_dir));
	Ff(tl, 0, "\tVRT_fini_dir(cli, VGC_backend_%.*s);\n", PF(t_dir));
}
