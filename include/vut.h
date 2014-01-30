/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
 * All rights reserved.
 *
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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
 * Common functions for the utilities
 */

#include "vdef.h"

typedef int VUT_cb_f(void);

struct VUT {
	const char	*progname;
	char		*name;

	/* Options */
	int		d_opt;
	int		D_opt;
	int		g_arg;
	char		*P_arg;
	char		*q_arg;
	char		*r_arg;

	/* State */
	struct VSL_data	*vsl;
	struct VSM_data	*vsm;
	struct VSLQ	*vslq;
	struct vpf_fh	*pfh;
	int		sighup;
	int		sigint;
	int		sigusr1;

	/* Callback functions */
	VUT_cb_f	*idle_f;
	VUT_cb_f	*sighup_f;
	VSLQ_dispatch_f	*dispatch_f;
	void		*dispatch_priv;
};

extern struct VUT VUT;

void VUT_Error(int status, const char *fmt, ...)
	__printflike(2, 3);

int VUT_g_Arg(const char *arg);

int VUT_Arg(int opt, const char *arg);

void VUT_Setup(void);

void VUT_Init(const char *progname);

void VUT_Fini(void);

int VUT_Main(void);
