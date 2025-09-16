/*-
 * Copyright (c) 2010-2014 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 */

/*lint -save -e525 -e539 */

VCC_KIND(NONE,		none)
VCC_KIND(RESERVED,	reserved)
VCC_KIND(ALIAS,		alias)
VCC_KIND(ACL,		acl)
VCC_KIND(ACTION,	action)
VCC_KIND(BACKEND,	backend)
VCC_KIND(FUNC,		func)
VCC_KIND(INSTANCE,	instance)
VCC_KIND(METHOD,	method)
VCC_KIND(OBJECT,	object)
VCC_KIND(PROBE,		probe)
VCC_KIND(STEVEDORE,	stevedore)
VCC_KIND(SUB,		sub)
VCC_KIND(VAR,		var)
VCC_KIND(VCL,		vcl)
VCC_KIND(VMOD,		vmod)
VCC_KIND(CLI_METHOD,	cli)
#undef VCC_KIND

/*lint -restore */
