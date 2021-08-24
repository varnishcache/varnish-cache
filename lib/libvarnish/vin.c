/*-
 * Copyright (c) 2007-2010 Varnish Software AS
 * All rights reserved.
 *
 * Author: Dag-Erling Smørgrav <des@des.no>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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
 * NB: also used in libvarnishapi
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vdef.h"

#include "vas.h"
#include "vin.h"
#include "vsb.h"

char *
VIN_n_Arg(const char *n_arg)
{
	struct vsb *vsb;
	char *retval;

	vsb = VSB_new_auto();
	AN(vsb);
	if (n_arg == NULL || n_arg[0] == '\0') {
		VSB_cat(vsb, VARNISH_STATE_DIR);
		VSB_cat(vsb, "/varnishd");
	} else if (n_arg[0] == '/') {
		VSB_cat(vsb, n_arg);
	} else {
		VSB_cat(vsb, VARNISH_STATE_DIR);
		VSB_cat(vsb, "/");
		VSB_cat(vsb, n_arg);
	}
	AZ(VSB_finish(vsb));

	retval = strdup(VSB_data(vsb));
	VSB_destroy(&vsb);
	return (retval);
}
