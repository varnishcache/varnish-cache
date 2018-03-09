/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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

struct VUT;
struct vopt_spec;

typedef void VUT_sighandler_f(int);
typedef int VUT_cb_f(struct VUT *);
typedef void VUT_error_f(struct VUT *, int, const char *, va_list);

struct VUT {
	unsigned	magic;
#define VUT_MAGIC	0xdf3b3de8
	const char	*progname;

	/* Options */
	int		d_opt;
	int		D_opt;
	int		g_arg;
	int		k_arg;
	char		*n_arg;
	char		*P_arg;
	char		*q_arg;
	char		*r_arg;
	char		*t_arg;

	/* State */
	struct VSL_data	*vsl;
	struct vsm	*vsm;
	struct VSLQ	*vslq;
	int		sighup;
	int		sigint;
	int		sigusr1;

	/* Callback functions */
	VUT_cb_f	*idle_f;
	VUT_cb_f	*sighup_f;
	VUT_error_f	*error_f;
	VSLQ_dispatch_f	*dispatch_f;
	void		*dispatch_priv;
};

void VUT_Error(struct VUT *, int status, const char *fmt, ...)
    v_noreturn_ v_printflike_(3, 4);

int VUT_Arg(struct VUT *, int opt, const char *arg);

#define VUT_InitProg(argc, argv, spec) VUT_Init(argv[0], argc, argv, spec)

struct VUT * VUT_Init(const char *progname, int argc, char * const *argv,
    const struct vopt_spec *);

void VUT_Signal(VUT_sighandler_f);
void VUT_Signaled(struct VUT *, int);

void VUT_Setup(struct VUT *);
int  VUT_Main(struct VUT *);
void VUT_Fini(struct VUT **);
