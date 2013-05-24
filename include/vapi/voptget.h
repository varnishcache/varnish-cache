/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2013 Varnish Software AS
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
 */

/*
 * Legend: VOPT(o,s,r,d,l) where
 *   o: Option string part
 *   s: Synopsis
 *   d: Description
 *   l: Long description
 */

#ifdef VOPT_OPTSTRING
#define VOPT(o,s,d,l) o
const char vopt_optstring[] =
#include VOPT_INC
    ;
#undef VOPT
#endif

#ifdef VOPT_SYNOPSIS
#define VOPT(o,s,d,l) " " s
const char vopt_synopsis[] =
#include VOPT_INC
    ;
#undef VOPT
#endif

#ifdef VOPT_USAGE
#define VOPT(o,s,d,l) s, d,
const char *vopt_usage[] = {
#include VOPT_INC
	NULL, NULL,
};
#undef VOPT
#endif

#ifndef VOPTGET_H
#define VOPTGET_H

struct vopt_full {
	const char *option;
	const char *synopsis;
	const char *desc;
	const char *ldesc;
};

#endif

#ifdef VOPT_FULL
#define VOPT(o,s,d,l) { o,s,d,l },
const struct vopt_full vopt_full[] = {
#include VOPT_INC
};
#undef VOPT
#endif
