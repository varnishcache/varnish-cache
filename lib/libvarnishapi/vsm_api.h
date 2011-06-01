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

/* Parameters */
#define			SLEEP_USEC	(50*1000)
#define			TIMEOUT_USEC	(5*1000*1000)

struct vsc;

struct VSM_data {
	unsigned		magic;
#define VSM_MAGIC		0x6e3bd69b

	VSM_diag_f		*diag;
	void			*priv;

	char			*n_opt;
	char			*fname;


	struct stat		fstat;

	int			vsm_fd;
	struct VSM_head		*VSM_head;
	void			*vsm_end;
	unsigned		alloc_seq;

	/* Stuff relating the stats fields start here */

	struct vsc		*vsc;
	struct vsl		*vsl;
};

struct VSM_chunk *VSM_find_alloc(struct VSM_data *vd, const char *class,
    const char *type, const char *ident);

void VSC_Delete(struct VSM_data *vd);
void VSL_Delete(struct VSM_data *vd);
void VSL_Open_CallBack(struct VSM_data *vd);
