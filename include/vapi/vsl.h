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

#ifndef VAPI_VSL_H_INCLUDED
#define VAPI_VSL_H_INCLUDED

#include "vapi/vsl_int.h"

struct VSM_data;

/*---------------------------------------------------------------------
 * VSL level access functions
 */

void VSL_Setup(struct VSM_data *vd);
	/*
	 * Setup vd for use with VSL functions.
	 * Must be called once before any other VSL function is called.
	 */

int VSL_Open(struct VSM_data *vd, int diag);
	/*
	 * Attempt to open the VSM (unless -r given)
	 * If diag is non-zero, diagnostics are emitted.
	 * Returns:
	 *	0 on success
	 *	!= 0 on failure
	 */

#define VSL_ARGS	"bCcdI:i:k:n:r:s:X:x:m:"
#define VSL_b_USAGE	"[-b]"
#define VSL_c_USAGE	"[-c]"
#define VSL_C_USAGE	"[-C]"
#define VSL_d_USAGE	"[-d]"
#define VSL_i_USAGE	"[-i tag]"
#define VSL_I_USAGE	"[-I regexp]"
#define VSL_k_USAGE	"[-k keep]"
#define VSL_m_USAGE	"[-m tag:regex]"
#define VSL_n_USAGE	VSM_n_USAGE
#define VSL_r_USAGE	"[-r file]"
#define VSL_s_USAGE	"[-s skip]"
#define VSL_x_USAGE	"[-x tag]"
#define VSL_X_USAGE	"[-X regexp]"
#define VSL_USAGE	"[-bCcd] "		\
			VSL_i_USAGE " "		\
			VSL_I_USAGE " "		\
			VSL_k_USAGE " "		\
			VSL_m_USAGE " "		\
			VSL_n_USAGE " "		\
			VSL_r_USAGE " "		\
			VSL_s_USAGE " "		\
			VSL_X_USAGE " "		\
			VSL_x_USAGE

int VSL_Arg(struct VSM_data *vd, int arg, const char *opt);
	/*
	 * Handle standard log-presenter arguments
	 * Return:
	 *	-1 error
	 *	 0 not handled
	 *	 1 Handled.
	 */

typedef int VSL_handler_f(void *priv, enum VSL_tag_e tag, unsigned fd,
    unsigned len, unsigned spec, const char *ptr, uint64_t bitmap);

#define VSL_S_CLIENT	(1 << 0)
#define VSL_S_BACKEND	(1 << 1)
VSL_handler_f VSL_H_Print;
struct VSM_data;
void VSL_Select(const struct VSM_data *vd, unsigned tag);
void VSL_NonBlocking(const struct VSM_data *vd, int nb);
int VSL_Dispatch(struct VSM_data *vd, VSL_handler_f *func, void *priv);
int VSL_NextLog(const struct VSM_data *lh, uint32_t **pp, uint64_t *bitmap);
int VSL_Matched(const struct VSM_data *vd, uint64_t bitmap);
int VSL_Name2Tag(const char *name, int l);
extern const char *VSL_tags[256];

#endif /* VAPI_VSL_H_INCLUDED */
