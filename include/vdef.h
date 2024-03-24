/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2012 Fastly Inc
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Rogier 'DocWilco' Mulhuijzen <rogier@fastly.com>
 *
 * Inspired by FreeBSD's <sys/cdefs.h>
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
 * Names of the form "v_[a-z_]*_" is reserved for this file.
 *
 * This file should always be the first non <...> include in a .c file.
 */

#include <stddef.h>

#ifdef VDEF_H_INCLUDED
#  error "vdef.h included multiple times"
#endif
#define VDEF_H_INCLUDED

/* Safe printf into a fixed-size buffer */
#define bprintf(buf, fmt, ...)						\
	do {								\
		int ibprintf;						\
		ibprintf = snprintf(buf, sizeof buf, fmt, __VA_ARGS__);	\
		assert(ibprintf >= 0 && ibprintf < (int)sizeof buf);	\
	} while (0)

/* Safe printf into a fixed-size buffer */
#define vbprintf(buf, fmt, ap)						\
	do {								\
		int ivbprintf;						\
		ivbprintf = vsnprintf(buf, sizeof buf, fmt, ap);	\
		assert(ivbprintf >= 0 && ivbprintf < (int)sizeof buf);	\
	} while (0)

/* Safe strcpy into a fixed-size buffer */
#define bstrcpy(dst, src)						\
	do {								\
		assert(strlen(src) + 1 <= sizeof (dst));		\
		strcpy((dst), (src));					\
	} while (0)

// TODO #define strcpy BANNED
// TODO then revert 0fa4baead49f0a45f68d3db0b7743c5e4e93ad4d
// TODO and replace with flexelint exception

/* Close and discard filedescriptor */
#define closefd(fdp)				\
	do {					\
		assert(*(fdp) >= 0);		\
		AZ(close(*(fdp)));		\
		*(fdp) = -1;			\
	} while (0)

#if !defined(__has_feature)
#  define __has_feature(x)	0
#endif

#ifndef __GNUC_PREREQ__
# if defined __GNUC__ && defined __GNUC_MINOR__
#  define __GNUC_PREREQ__(maj, min) \
	(__GNUC__ > (maj) || (__GNUC__ == (maj) && __GNUC_MINOR__ >= (min)))
# else
#  define __GNUC_PREREQ__(maj, min) 0
# endif
#endif

#if __GNUC_PREREQ__(2, 95) || defined(__INTEL_COMPILER)
#  define v_printflike_(f,a) __attribute__((format(printf, f, a)))
#else
#  define v_printflike_(f,a)
#endif

#define v_noreturn_ __attribute__((__noreturn__))

#ifdef __GNUC__
#  define v_deprecated_ __attribute__((deprecated))
#else
#  define v_deprecated_
#endif

#if __GNUC_PREREQ__(4,4) // added 2008-07-23
#  define v_dont_optimize __attribute__((optimize("O")))
#else
#  define v_dont_optimize
#endif

#ifdef HAVE___GCOV_FLUSH
#  define v_gcov_flush() __gcov_flush()
int __gcov_flush(void);
#elif defined HAVE___GCOV_DUMP
#  define v_gcov_flush() __gcov_dump()
void __gcov_dump(void);
#elif defined HAVE___LLVM_GCOV_FLUSH
#  define v_gcov_flush() __llvm_gcov_flush()
int __llvm_gcov_flush(void);
#else
#  define v_gcov_flush() do { } while (0)
#endif

/*********************************************************************
 * Fundamental numerical limits
  * These limits track RFC8941
 * We use hex notation because 999999999999.999 is not perfectly
 * representable in ieee64 doubles.
 */

#define VRT_INTEGER_MAX 999999999999999
#define VRT_INTEGER_MIN -999999999999999
#define VRT_DECIMAL_MAX 0x1.d1a94a1fffff8p+39
#define VRT_DECIMAL_MIN -0x1.d1a94a1fffff8p+39

/*********************************************************************
 * Pointer alignment magic
 */

#if defined(__sparc__)
/* NB: Overbroad test for 32bit userland on 64bit SPARC cpus. */
#  define PALGN	    (sizeof(double) - 1)	/* size of alignment */
#else
#  define PALGN	    (sizeof(void *) - 1)	/* size of alignment */
#endif
#define PAOK(p)	    (((uintptr_t)(p) & PALGN) == 0)	/* is aligned */
#define PRNDDN(p)   ((uintptr_t)(p) & ~PALGN)		/* Round down */
#define PRNDUP(p)   (((uintptr_t)(p) + PALGN) & ~PALGN)	/* Round up */

/*********************************************************************
 * To be used as little as possible to wash off const/volatile etc.
 */
#define TRUST_ME(ptr)	((void*)(uintptr_t)(ptr))

/**********************************************************************
 * Generic power-2 rounding macros
 */

#define PWR2(x)     ((((x)-1UL)&(x))==0)		/* Is a power of two */
#define RDN2(x, y)  ((x)&(~((uintptr_t)(y)-1UL)))	/* PWR2(y) true */
#define RUP2(x, y)  (((x)+((y)-1))&(~((uintptr_t)(y)-1UL))) /* PWR2(y) true */

/**********************************************************************
 * Find the minimum or maximum values.
 * Only evaluate the expression once and perform type checking.
 */

/* ref: https://stackoverflow.com/a/17624752 */

#define VINDIRECT(a, b, c)	a ## b ## c
#define VCOMBINE(a, b, c)	VINDIRECT(a, b, c)

#if defined(__COUNTER__)
#	define VUNIQ_NAME(base)	VCOMBINE(base, __LINE__, __COUNTER__)
#else
#	define VUNIQ_NAME(base)	VCOMBINE(base, __LINE__, 0)
#endif

#ifdef _lint
#define typeof(x) __typeof__(x)
#endif

/* ref: https://gcc.gnu.org/onlinedocs/gcc/Typeof.html */

#define _vtake(op, ta, tb, a, b, _va, _vb)				\
	({								\
	ta _va = (a);							\
	tb _vb = (b);							\
	(void)(&_va == &_vb);						\
	_va op _vb ? _va : _vb;						\
})

#define opmin <
#define opmax >
#define vtake(n, ta, tb, a, b)	_vtake(op ## n, ta, tb, a, b,		\
    VUNIQ_NAME(_v ## n ## A), VUNIQ_NAME(_v ## n ## B))

#define vmin(a, b)		vtake(min, typeof(a), typeof(b), a, b)
#define vmax(a, b)		vtake(max, typeof(a), typeof(b), a, b)

#define vmin_t(type, a, b)	vtake(min, type, type, a, b)
#define vmax_t(type, a, b)	vtake(max, type, type, a, b)

/**********************************************************************
 * Clamp the value between two limits.
 */

#define vlimit(a, l, u)		vmax((l), vmin((a), (u)))
#define vlimit_t(type, a, l, u)	vmax_t(type, (l), vmin_t(type, (a), (u)))

/**********************************************************************
 * FlexeLint and compiler shutuppery
 */

/*
 * In OO-light situations, functions have to match their prototype
 * even if that means not const'ing a const'able argument.
 * The typedef should be specified as argument to the macro.
 */
#define v_matchproto_(xxx)		/*lint --e{818} */

/*
 * State variables may change value before we have considered the
 * previous value
 */
#define v_statevariable_(varname)	varname /*lint -esym(838,varname) */

#ifdef __SUNPRO_C
#  define NEEDLESS(s)		{}
#else
#  define NEEDLESS(s)		s
#endif

#if __GNUC_PREREQ__(2, 7)
#  define v_unused_ __attribute__((__unused__))
#else
#  define v_unused_
#endif

#if __GNUC_PREREQ__(4, 3) || defined(__clang__)
#  define v_cold_    __attribute__((cold))
#else
#  define v_cold_
#endif

#if defined __has_attribute
#  if __has_attribute(counted_by)
#    define v_counted_by_(field) __attribute__((counted_by(field)))
#  endif
#endif

#ifndef v_counted_by_
#  define v_counted_by_(field)
#endif

/* VTIM API overhaul WIP */
typedef double vtim_mono;
typedef double vtim_real;
typedef double vtim_dur;

/**********************************************************************
 * txt (vas.h needed for the macros)
 */

typedef struct {
	const char		*b;
	const char		*e;
} txt;

#define Tcheck(t)	do { (void)pdiff((t).b, (t).e); } while (0)
#define Tlen(t)		(pdiff((t).b, (t).e))

/* #3020 dummy definitions until PR is merged*/
#define LIKELY(x)	(x)
#define UNLIKELY(x)	(x)
