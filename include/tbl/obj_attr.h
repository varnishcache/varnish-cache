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

/* upper, lower, size */
#ifdef OBJ_FIXATTR
  OBJ_FIXATTR(LEN, len, 8)
  OBJ_FIXATTR(VXID, vxid, 4)
  OBJ_FIXATTR(FLAGS, flags, 1)
  OBJ_FIXATTR(GZIPBITS, gzipbits, 32)
  OBJ_FIXATTR(LASTMODIFIED, lastmodified, 8)
  #undef OBJ_FIXATTR
#endif

/* upper, lower */
#ifdef OBJ_VARATTR
  OBJ_VARATTR(VARY, vary)
  OBJ_VARATTR(HEADERS, headers)
  #undef OBJ_VARATTR
#endif

/* upper, lower */
#ifdef OBJ_AUXATTR
  OBJ_AUXATTR(ESIDATA, esidata)
  #undef OBJ_AUXATTR
#endif

#ifdef OBJ_FLAG
/* upper, lower, val */
  OBJ_FLAG(GZIPED,	gziped,		(1<<1))
  OBJ_FLAG(CHGGZIP,	chggzip,	(1<<2))
  OBJ_FLAG(IMSCAND,	imscand,	(1<<3))
  OBJ_FLAG(ESIPROC,	esiproc,	(1<<4))
  #undef OBJ_FLAG
#endif

/*lint -restore */
