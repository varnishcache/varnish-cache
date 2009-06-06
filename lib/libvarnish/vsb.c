/*-
 * Copyright (c) 2000 Poul-Henning Kamp and Dag-Erling Smørgrav
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * $FreeBSD: src/sys/kern/subr_sbuf.c,v 1.30 2005/12/23 11:49:53 phk Exp $
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libvarnish.h"
#include "vsb.h"

#define	KASSERT(e, m)		assert(e)
#define	SBMALLOC(size)		malloc(size)
#define	SBFREE(buf)		free(buf)
#define	min(x,y)		(x < y ? x : y)

#define VSB_MAGIC		0x4a82dd8a
/*
 * Predicates
 */
#define	VSB_ISDYNAMIC(s)	((s)->s_flags & VSB_DYNAMIC)
#define	VSB_ISDYNSTRUCT(s)	((s)->s_flags & VSB_DYNSTRUCT)
#define	VSB_ISFINISHED(s)	((s)->s_flags & VSB_FINISHED)
#define	VSB_HASOVERFLOWED(s)	((s)->s_flags & VSB_OVERFLOWED)
#define	VSB_HASROOM(s)		((s)->s_len < (s)->s_size - 1)
#define	VSB_FREESPACE(s)	((s)->s_size - ((s)->s_len + 1))
#define	VSB_CANEXTEND(s)	((s)->s_flags & VSB_AUTOEXTEND)

/*
 * Set / clear flags
 */
#define	VSB_SETFLAG(s, f)	do { (s)->s_flags |= (f); } while (0)
#define	VSB_CLEARFLAG(s, f)	do { (s)->s_flags &= ~(f); } while (0)

#define	VSB_MINEXTENDSIZE	16		/* Should be power of 2. */
#define	VSB_MAXEXTENDSIZE	4096
#define	VSB_MAXEXTENDINCR	4096

/*
 * Debugging support
 */
#if !defined(NDEBUG)
static void
_vsb_assert_integrity(const char *fun, struct vsb *s)
{
	(void)fun;
	(void)s;
	KASSERT(s != NULL,
	    ("%s called with a NULL vsb pointer", fun));
	KASSERT(s->s_magic == VSB_MAGIC,
	    ("%s called wih an unintialized vsb pointer", fun));
	KASSERT(s->s_buf != NULL,
	    ("%s called with uninitialized or corrupt vsb", fun));
	KASSERT(s->s_len < s->s_size,
	    ("wrote past end of vsb (%d >= %d)", s->s_len, s->s_size));
}

static void
_vsb_assert_state(const char *fun, struct vsb *s, int state)
{
	(void)fun;
	(void)s;
	(void)state;
	KASSERT((s->s_flags & VSB_FINISHED) == state,
	    ("%s called with %sfinished or corrupt vsb", fun,
	    (state ? "un" : "")));
}
#define	vsb_assert_integrity(s) _vsb_assert_integrity(__func__, (s))
#define	vsb_assert_state(s, i)	 _vsb_assert_state(__func__, (s), (i))
#else
#define	vsb_assert_integrity(s) do { } while (0)
#define	vsb_assert_state(s, i)	 do { } while (0)
#endif

static int
vsb_extendsize(int size)
{
	int newsize;

	newsize = VSB_MINEXTENDSIZE;
	while (newsize < size) {
		if (newsize < (int)VSB_MAXEXTENDSIZE)
			newsize *= 2;
		else
			newsize += VSB_MAXEXTENDINCR;
	}

	return (newsize);
}


/*
 * Extend an vsb.
 */
static int
vsb_extend(struct vsb *s, int addlen)
{
	char *newbuf;
	int newsize;

	if (!VSB_CANEXTEND(s))
		return (-1);

	newsize = vsb_extendsize(s->s_size + addlen);
	newbuf = (char *)SBMALLOC(newsize);
	if (newbuf == NULL)
		return (-1);
	memcpy(newbuf, s->s_buf, s->s_size);
	if (VSB_ISDYNAMIC(s))
		SBFREE(s->s_buf);
	else
		VSB_SETFLAG(s, VSB_DYNAMIC);
	s->s_buf = newbuf;
	s->s_size = newsize;
	return (0);
}

/*
 * Initialize an vsb.
 * If buf is non-NULL, it points to a static or already-allocated string
 * big enough to hold at least length characters.
 */
struct vsb *
vsb_new(struct vsb *s, char *buf, int length, int flags)
{
	KASSERT(length >= 0,
	    ("attempt to create an vsb of negative length (%d)", length));
	KASSERT((flags & ~VSB_USRFLAGMSK) == 0,
	    ("%s called with invalid flags", __func__));

	flags &= VSB_USRFLAGMSK;
	if (s == NULL) {
		s = (struct vsb *)SBMALLOC(sizeof *s);
		if (s == NULL)
			return (NULL);
		if (vsb_new(s, buf, length, flags) == NULL) {
			free(s);
			return (NULL);
		}
		VSB_SETFLAG(s, VSB_DYNSTRUCT);
		return (s);
	}

	memset(s, 0, sizeof *s);
	s->s_flags = flags;
	s->s_magic = VSB_MAGIC;
	s->s_size = length;
	if (buf) {
		s->s_buf = buf;
		return (s);
	}
	if (flags & VSB_AUTOEXTEND)
		s->s_size = vsb_extendsize(s->s_size);
	s->s_buf = (char *)SBMALLOC(s->s_size);
	if (s->s_buf == NULL)
		return (NULL);
	VSB_SETFLAG(s, VSB_DYNAMIC);
	return (s);
}

/*
 * Clear an vsb and reset its position.
 */
void
vsb_clear(struct vsb *s)
{
	vsb_assert_integrity(s);
	/* don't care if it's finished or not */

	VSB_CLEARFLAG(s, VSB_FINISHED);
	VSB_CLEARFLAG(s, VSB_OVERFLOWED);
	s->s_len = 0;
}

/*
 * Set the vsb's end position to an arbitrary value.
 * Effectively truncates the vsb at the new position.
 */
int
vsb_setpos(struct vsb *s, int pos)
{
	vsb_assert_integrity(s);
	vsb_assert_state(s, 0);

	KASSERT(pos >= 0,
	    ("attempt to seek to a negative position (%d)", pos));
	KASSERT(pos < s->s_size,
	    ("attempt to seek past end of vsb (%d >= %d)", pos, s->s_size));

	if (pos < 0 || pos > s->s_len)
		return (-1);
	s->s_len = pos;
	return (0);
}

/*
 * Append a byte string to an vsb.
 */
int
vsb_bcat(struct vsb *s, const void *buf, size_t len)
{
	const char *str = buf;

	vsb_assert_integrity(s);
	vsb_assert_state(s, 0);

	if (VSB_HASOVERFLOWED(s))
		return (-1);

	for (; len; len--) {
		if (!VSB_HASROOM(s) && vsb_extend(s, len) < 0)
			break;
		s->s_buf[s->s_len++] = *str++;
	}
	if (len) {
		VSB_SETFLAG(s, VSB_OVERFLOWED);
		return (-1);
	}
	return (0);
}

/*
 * Copy a byte string into an vsb.
 */
int
vsb_bcpy(struct vsb *s, const void *buf, size_t len)
{
	vsb_assert_integrity(s);
	vsb_assert_state(s, 0);

	vsb_clear(s);
	return (vsb_bcat(s, buf, len));
}

/*
 * Append a string to an vsb.
 */
int
vsb_cat(struct vsb *s, const char *str)
{
	vsb_assert_integrity(s);
	vsb_assert_state(s, 0);

	if (VSB_HASOVERFLOWED(s))
		return (-1);

	while (*str) {
		if (!VSB_HASROOM(s) && vsb_extend(s, strlen(str)) < 0)
			break;
		s->s_buf[s->s_len++] = *str++;
	}
	if (*str) {
		VSB_SETFLAG(s, VSB_OVERFLOWED);
		return (-1);
	}
	return (0);
}

/*
 * Copy a string into an vsb.
 */
int
vsb_cpy(struct vsb *s, const char *str)
{
	vsb_assert_integrity(s);
	vsb_assert_state(s, 0);

	vsb_clear(s);
	return (vsb_cat(s, str));
}

/*
 * Format the given argument list and append the resulting string to an vsb.
 */
int
vsb_vprintf(struct vsb *s, const char *fmt, va_list ap)
{
	va_list ap_copy;
	int len;

	vsb_assert_integrity(s);
	vsb_assert_state(s, 0);

	KASSERT(fmt != NULL,
	    ("%s called with a NULL format string", __func__));

	if (VSB_HASOVERFLOWED(s))
		return (-1);

	do {
		va_copy(ap_copy, ap);
		len = vsnprintf(&s->s_buf[s->s_len], VSB_FREESPACE(s) + 1,
		    fmt, ap_copy);
		va_end(ap_copy);
	} while (len > VSB_FREESPACE(s) &&
	    vsb_extend(s, len - VSB_FREESPACE(s)) == 0);

	/*
	 * s->s_len is the length of the string, without the terminating nul.
	 * When updating s->s_len, we must subtract 1 from the length that
	 * we passed into vsnprintf() because that length includes the
	 * terminating nul.
	 *
	 * vsnprintf() returns the amount that would have been copied,
	 * given sufficient space, hence the min() calculation below.
	 */
	s->s_len += min(len, VSB_FREESPACE(s));
	if (!VSB_HASROOM(s) && !VSB_CANEXTEND(s))
		VSB_SETFLAG(s, VSB_OVERFLOWED);

	KASSERT(s->s_len < s->s_size,
	    ("wrote past end of vsb (%d >= %d)", s->s_len, s->s_size));

	if (VSB_HASOVERFLOWED(s))
		return (-1);
	return (0);
}

/*
 * Format the given arguments and append the resulting string to an vsb.
 */
int
vsb_printf(struct vsb *s, const char *fmt, ...)
{
	va_list ap;
	int result;

	va_start(ap, fmt);
	result = vsb_vprintf(s, fmt, ap);
	va_end(ap);
	return(result);
}

/*
 * Append a character to an vsb.
 */
int
vsb_putc(struct vsb *s, int c)
{
	vsb_assert_integrity(s);
	vsb_assert_state(s, 0);

	if (VSB_HASOVERFLOWED(s))
		return (-1);

	if (!VSB_HASROOM(s) && vsb_extend(s, 1) < 0) {
		VSB_SETFLAG(s, VSB_OVERFLOWED);
		return (-1);
	}
	if (c != '\0')
	    s->s_buf[s->s_len++] = (char)c;
	return (0);
}

/*
 * Trim whitespace characters from end of an vsb.
 */
int
vsb_trim(struct vsb *s)
{
	vsb_assert_integrity(s);
	vsb_assert_state(s, 0);

	if (VSB_HASOVERFLOWED(s))
		return (-1);

	while (s->s_len && isspace(s->s_buf[s->s_len-1]))
		--s->s_len;

	return (0);
}

/*
 * Check if an vsb overflowed
 */
int
vsb_overflowed(const struct vsb *s)
{
    return VSB_HASOVERFLOWED(s);
}

/*
 * Finish off an vsb.
 */
void
vsb_finish(struct vsb *s)
{
	vsb_assert_integrity(s);
	vsb_assert_state(s, 0);

	s->s_buf[s->s_len] = '\0';
	VSB_CLEARFLAG(s, VSB_OVERFLOWED);
	VSB_SETFLAG(s, VSB_FINISHED);
}

/*
 * Return a pointer to the vsb data.
 */
char *
vsb_data(struct vsb *s)
{
	vsb_assert_integrity(s);
	vsb_assert_state(s, VSB_FINISHED);

	return s->s_buf;
}

/*
 * Return the length of the vsb data.
 */
int
vsb_len(struct vsb *s)
{
	vsb_assert_integrity(s);
	/* don't care if it's finished or not */

	if (VSB_HASOVERFLOWED(s))
		return (-1);
	return s->s_len;
}

/*
 * Clear an vsb, free its buffer if necessary.
 */
void
vsb_delete(struct vsb *s)
{
	int isdyn;

	vsb_assert_integrity(s);
	/* don't care if it's finished or not */

	if (VSB_ISDYNAMIC(s))
		SBFREE(s->s_buf);
	isdyn = VSB_ISDYNSTRUCT(s);
	memset(s, 0, sizeof *s);
	if (isdyn)
		SBFREE(s);
}

/*
 * Check if an vsb has been finished.
 */
int
vsb_done(const struct vsb *s)
{

	return(VSB_ISFINISHED(s));
}

/*
 * Quote a string
 */
void
vsb_quote(struct vsb *s, const char *p, int how)
{
	const char *q;
	int quote = 0;

	(void)how;	/* For future enhancements */

	for (q = p; *q != '\0'; q++) {
		if (!isgraph(*q) || *q == '"') {
			quote++;
			break;
		}
	}
	if (!quote) {
		(void)vsb_cat(s, p);
		return;
	}
	(void)vsb_putc(s, '"');
	for (q = p; *q != '\0'; q++) {
		switch (*q) {
		case ' ':
			(void)vsb_putc(s, *q);
			break;
		case '\\':
		case '"':
			(void)vsb_putc(s, '\\');
			(void)vsb_putc(s, *q);
			break;
		case '\n':
			(void)vsb_cat(s, "\\n");
			break;
		case '\r':
			(void)vsb_cat(s, "\\r");
			break;
		case '\t':
			(void)vsb_cat(s, "\\t");
			break;
		default:
			if (isgraph(*q))
				(void)vsb_putc(s, *q);
			else
				(void)vsb_printf(s, "\\%o", *q);
			break;
		}
	}
	(void)vsb_putc(s, '"');
}
