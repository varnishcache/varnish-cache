/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Redpill Linpro AS
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

/* Parameters */
#define			SLEEP_USEC	(50*1000)
#define			TIMEOUT_USEC	(5*1000*1000)

struct vsl_sf {
	unsigned		magic;
#define VSL_SF_MAGIC		0x558478dd
	VTAILQ_ENTRY(vsl_sf)	next;
	int			flags;
#define VSL_SF_EXCL		(1 << 0)
#define VSL_SF_CL_WC		(1 << 1)
#define VSL_SF_ID_WC		(1 << 2)
#define VSL_SF_NM_WC		(1 << 3)
	char			*class;
	char			*ident;
	char			*name;
};

struct VSM_data {
	unsigned		magic;
#define VSM_MAGIC		0x6e3bd69b

	vsm_diag_f		*diag;
	void			*priv;

	char			*n_opt;
	char			*fname;


	struct stat		fstat;

	int			vsl_fd;
	struct vsm_head	*vsl_lh;
	void			*vsl_end;
	unsigned		alloc_seq;

	/* Stuff relating the stats fields start here */

	int			sf_init;
	VTAILQ_HEAD(, vsl_sf)	sf_list;

	/* Stuff relating the log records below here */

	volatile uint32_t	*log_start;
	volatile uint32_t	*log_end;
	volatile uint32_t	*log_ptr;

	volatile uint32_t	last_seq;

	/* for -r option */
	int			r_fd;
	unsigned		rbuflen;
	uint32_t		*rbuf;

	unsigned		L_opt;
	int			b_opt;
	int			c_opt;
	int			d_opt;

	unsigned		flags;
#define F_SEEN_IX		(1 << 0)
#define F_NON_BLOCKING		(1 << 1)

	/*
	 * These two bitmaps mark fd's as belonging to client or backend
	 * transactions respectively.
	 */
	struct vbitmap		*vbm_client;
	struct vbitmap		*vbm_backend;

	/*
	 * Bit map of programatically selected tags, that cannot be suppressed.
	 * This way programs can make sure they will see certain tags, even
	 * if the user tries to supress them with -x/-X
	 */
	struct vbitmap		*vbm_select;	/* index: tag */

	/* Bit map of tags selected/supressed with -[iIxX] options */
	struct vbitmap		*vbm_supress;	/* index: tag */

	int			regflags;
	vre_t			*regincl;
	vre_t			*regexcl;

	unsigned long		skip;
	unsigned long		keep;
};

struct vsm_chunk *vsm_find_alloc(const struct VSM_data *vd, const char *class,
    const char *type, const char *ident);
