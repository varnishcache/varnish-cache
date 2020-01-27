/*-
 * Copyright 2017 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Author: Nils Goroll <nils.goroll@uplex.de>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Define which operators are valid for which ban arguments
 */

/*lint -save -e525 -e539 */

#define BANS_OPER_STRING			\
	(1<<BAN_OPERIDX(BANS_OPER_EQ) |	\
	 1<<BAN_OPERIDX(BANS_OPER_NEQ) |	\
	 1<<BAN_OPERIDX(BANS_OPER_MATCH) |	\
	 1<<BAN_OPERIDX(BANS_OPER_NMATCH))

#define BANS_OPER_DURATION			\
	(1<<BAN_OPERIDX(BANS_OPER_EQ) |		\
	 1<<BAN_OPERIDX(BANS_OPER_NEQ) |	\
	 1<<BAN_OPERIDX(BANS_OPER_GT) |		\
	 1<<BAN_OPERIDX(BANS_OPER_GTE) |	\
	 1<<BAN_OPERIDX(BANS_OPER_LT) |		\
	 1<<BAN_OPERIDX(BANS_OPER_LTE))

ARGOPER(BANS_ARG_URL,		BANS_OPER_STRING)
ARGOPER(BANS_ARG_REQHTTP,	BANS_OPER_STRING)
ARGOPER(BANS_ARG_OBJHTTP,	BANS_OPER_STRING)	// INT?
ARGOPER(BANS_ARG_OBJSTATUS,	BANS_OPER_STRING)
ARGOPER(BANS_ARG_OBJTTL,	BANS_OPER_DURATION)
ARGOPER(BANS_ARG_OBJAGE,	BANS_OPER_DURATION)
ARGOPER(BANS_ARG_OBJGRACE,	BANS_OPER_DURATION)
ARGOPER(BANS_ARG_OBJKEEP,	BANS_OPER_DURATION)

#undef ARGOPER
#undef BANS_OPER_STRING
#undef BANS_OPER_DURATION

/*lint -restore */
