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

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "mgt/mgt.h"
#include "common/heritage.h"

#include "hash/hash_slinger.h"
#include "vav.h"

static const struct choice hsh_choice[] = {
	{ "classic",		&hcl_slinger },
	{ "simple",		&hsl_slinger },
	{ "simple_list",	&hsl_slinger },	/* backwards compat */
	{ "critbit",		&hcb_slinger },
	{ NULL,			NULL }
};

/*--------------------------------------------------------------------*/

void
HSH_config(const char *h_arg)
{
	char **av;
	int ac;
	const struct hash_slinger *hp;

	ASSERT_MGT();
	av = VAV_Parse(h_arg, NULL, ARGV_COMMA);
	AN(av);

	if (av[0] != NULL)
		ARGV_ERR("%s\n", av[0]);

	if (av[1] == NULL)
		ARGV_ERR("-h argument is empty\n");

	for (ac = 0; av[ac + 2] != NULL; ac++)
		continue;

	hp = pick(hsh_choice, av[1], "hash");
	CHECK_OBJ_NOTNULL(hp, SLINGER_MAGIC);
	VSB_printf(vident, ",-h%s", av[1]);
	heritage.hash = hp;
	if (hp->init != NULL)
		hp->init(ac, av + 2);
	else if (ac > 0)
		ARGV_ERR("Hash method \"%s\" takes no arguments\n",
		    hp->name);
	/* NB: Don't free av, the hasher is allowed to keep it. */
}
