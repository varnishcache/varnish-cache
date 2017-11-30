/*-
 * Copyright (c) 2000-2011 Poul-Henning Kamp
 * Copyright (c) 2000-2008 Dag-Erling Coïdan Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      $FreeBSD: head/sys/sys/vsb.h 221993 2011-05-16 16:18:40Z phk $
 */

#ifndef VSB_H_INCLUDED
#define VSB_H_INCLUDED

/*
 * Structure definition
 */
struct vsb {
	unsigned	magic;
#define VSB_MAGIC	0x4a82dd8a
	int		 s_error;	/* current error code */
	char		*s_buf;		/* storage buffer */
	ssize_t		 s_size;	/* size of storage buffer */
	ssize_t		 s_len;		/* current length of string */
#define	VSB_FIXEDLEN	0x00000000	/* fixed length buffer (default) */
#define	VSB_AUTOEXTEND	0x00000001	/* automatically extend buffer */
#define	VSB_USRFLAGMSK	0x0000ffff	/* mask of flags the user may specify */
#define	VSB_DYNAMIC	0x00010000	/* s_buf must be freed */
#define	VSB_FINISHED	0x00020000	/* set by VSB_finish() */
#define	VSB_DYNSTRUCT	0x00080000	/* vsb must be freed */
	int		 s_flags;	/* flags */
	int		 s_indent;	/* Ident level */
};

#ifdef __cplusplus
extern "C" {
#endif
/*
 * API functions
 */
struct vsb	*VSB_new(struct vsb *, char *, int, int);
#define		 VSB_new_auto()				\
	VSB_new(NULL, NULL, 0, VSB_AUTOEXTEND)
void		 VSB_clear(struct vsb *);
int		 VSB_bcat(struct vsb *, const void *, ssize_t);
int		 VSB_cat(struct vsb *, const char *);
int		 VSB_printf(struct vsb *, const char *, ...)
	v_printflike_(2, 3);
#ifdef va_start
int		 VSB_vprintf(struct vsb *, const char *, va_list)
	v_printflike_(2, 0);
#endif
int		 VSB_putc(struct vsb *, int);
int		 VSB_error(const struct vsb *);
int		 VSB_finish(struct vsb *);
char		*VSB_data(const struct vsb *);
ssize_t		 VSB_len(const struct vsb *);
void		 VSB_delete(struct vsb *);
void		 VSB_destroy(struct vsb **);
#define VSB_QUOTE_NONL		1
#define VSB_QUOTE_JSON		2
#define VSB_QUOTE_HEX		4
#define VSB_QUOTE_CSTR		8
#define VSB_QUOTE_UNSAFE	16
void		 VSB_quote_pfx(struct vsb *, const char*, const void *,
		     int len, int how);
void		 VSB_quote(struct vsb *, const void *, int len, int how);
void		 VSB_indent(struct vsb *, int);
#ifdef __cplusplus
};
#endif

#endif
