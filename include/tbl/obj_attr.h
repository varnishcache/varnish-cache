/*-
 * Copyright (c) 2014 Varnish Software AS
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

/*lint -save -e525 -e539 */

/* upper, lower */
#ifdef OBJ_ATTR
OBJ_ATTR(VXID,		vxid)
OBJ_ATTR(EXP,		exp)
OBJ_ATTR(VARY,		vary)
OBJ_ATTR(HEADERS,	headers)
OBJ_ATTR(FLAGS,		flags)
OBJ_ATTR(GZIPBITS,	gzipbits)
OBJ_ATTR(ESIDATA,	esidata)
OBJ_ATTR(LASTMODIFIED,	lastmodified)
#endif

#ifdef OBJ_FLAG
/* upper, lower, val */
OBJ_FLAG(GZIPED,	gziped,		(1<<1))
OBJ_FLAG(CHGGZIP,	chggzip,	(1<<2))
OBJ_FLAG(IMSCAND,	imscand,	(1<<3))
OBJ_FLAG(ESIPROC,	esiproc,	(1<<4))
#endif

/*lint -restore */
