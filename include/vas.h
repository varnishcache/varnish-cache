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
 * assert(), AN() and AZ() are static checks that should not happen.
 *	In general asserts should be cheap, such as checking return
 *	values and similar.
 * diagnostic() are asserts which are so expensive that we may want
 *	to compile them out for performance at a later date.
 * xxxassert(), XXXAN() and XXXAZ() marks conditions we ought to
 *	handle gracefully, such as malloc failure.
 */

#ifndef VAS_H_INCLUDED
#define VAS_H_INCLUDED

enum vas_e {
	VAS_WRONG,
	VAS_MISSING,
	VAS_ASSERT,
	VAS_INCOMPLETE,
	VAS_VCL,
};

typedef void vas_f(const char *, const char *, int, const char *, int,
    enum vas_e);

extern vas_f *VAS_Fail __attribute__((__noreturn__));

#ifdef WITHOUT_ASSERTS
#define assert(e)	((void)(e))
#else /* WITH_ASSERTS */
#define assert(e)							\
do {									\
	if (!(e)) {							\
		VAS_Fail(__func__, __FILE__, __LINE__,			\
		    #e, errno, VAS_ASSERT);				\
	}								\
} while (0)
#endif

#define xxxassert(e)							\
do {									\
	if (!(e)) {							\
		VAS_Fail(__func__, __FILE__, __LINE__,			\
		    #e, errno, VAS_MISSING);				\
	}								\
} while (0)

/* Assert zero return value */
#define AZ(foo)		do { assert((foo) == 0); } while (0)
#define AN(foo)		do { assert((foo) != 0); } while (0)
#define XXXAZ(foo)	do { xxxassert((foo) == 0); } while (0)
#define XXXAN(foo)	do { xxxassert((foo) != 0); } while (0)
#define diagnostic(foo)	assert(foo)
#define WRONG(expl)							\
do {									\
	VAS_Fail(__func__, __FILE__, __LINE__, expl, errno, VAS_WRONG);	\
} while (0)

#define INCOMPL()							\
do {									\
	VAS_Fail(__func__, __FILE__, __LINE__,				\
	    "", errno, VAS_INCOMPLETE);					\
} while (0)

#endif
