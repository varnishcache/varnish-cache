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

#ifdef __printflike
#  define v_printflike_(f,a) __printflike(f,a)
#elif __GNUC_PREREQ__(2, 95) || defined(__INTEL_COMPILER)
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

/* ref: https://gcc.gnu.org/onlinedocs/gcc/Typeof.html */

#define _vmin(a, b, _va, _vb)						\
({									\
	typeof (a) _va = (a);						\
	typeof (b) _vb = (b);						\
	(void)(&_va == &_vb);						\
	_va < _vb ? _va : _vb;						\
})

#define _vmax(a, b, _va, _vb)						\
({									\
	typeof (a) _va = (a);						\
	typeof (b) _vb = (b);						\
	(void)(&_va == &_vb);						\
	_va > _vb ? _va : _vb;						\
})

#define vmin(a, b)		_vmin((a), (b), VUNIQ_NAME(_vmina),	\
    VUNIQ_NAME(_vminb))
#define vmax(a, b)		_vmax((a), (b), VUNIQ_NAME(_vmaxa),	\
    VUNIQ_NAME(_vmaxb))

#define vmin_t(type, a, b)	vmin((type)(a), (type)(b))
#define vmax_t(type, a, b)	vmax((type)(a), (type)(b))

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
#define NEEDLESS(s)		{}
#else
#define NEEDLESS(s)		s
#endif

#if __GNUC_PREREQ__(2, 7)
#  define v_unused_ __attribute__((__unused__))
#else
#  define v_unused_
#endif

/*
 * Most of this nightmare is stolen from FreeBSD's <cdefs.h>
 */
#ifndef __has_extension
#  define __has_extension(x)	0
#endif

#if defined(_Static_assert)
    /* Nothing, somebody already did this for us */
#elif __has_extension(c_static_assert)
    /* Nothing, we should be fine */
#elif (defined(__cplusplus) && __cplusplus >= 201103L) || \
       __has_extension(cxx_static_assert)
#   define _Static_assert(x, y)	static_assert(x, y)
#elif __GNUC_PREREQ__(4,6) && !defined(__cplusplus)
    /* Nothing, gcc 4.6 and higher has _Static_assert built-in */
#else
#   if defined(__COUNTER__)
#	define _Static_assert(x, y)	__Static_assert(x, __COUNTER__)
#   else
#	define _Static_assert(x, y)	__Static_assert(x, __LINE__)
#   endif
#   define __Static_assert(x, y)	___Static_assert(x, y)
#   define ___Static_assert(x, y) \
		typedef char __assert_## y[(x) ? 1 : -1] v_unused_
#endif

/* VTIM API overhaul WIP */
typedef double vtim_mono;
typedef double vtim_real;
typedef double vtim_dur;
