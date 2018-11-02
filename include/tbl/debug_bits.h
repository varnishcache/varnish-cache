/*-
 * Copyright (c) 2012 Varnish Software AS
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
 * Fields in the debug parameter
 *
 */

/*lint -save -e525 -e539 */

DEBUG_BIT(REQ_STATE,		req_state,	"VSL Request state engine")
DEBUG_BIT(WORKSPACE,		workspace,	"VSL Workspace operations")
DEBUG_BIT(WAITER,		waiter,		"VSL Waiter internals")
DEBUG_BIT(WAITINGLIST,		waitinglist,	"VSL Waitinglist events")
DEBUG_BIT(SYNCVSL,		syncvsl,	"Make VSL synchronous")
DEBUG_BIT(HASHEDGE,		hashedge,	"Edge cases in Hash")
DEBUG_BIT(VCLREL,		vclrel,		"Rapid VCL release")
DEBUG_BIT(LURKER,		lurker,		"VSL Ban lurker")
DEBUG_BIT(ESI_CHOP,		esi_chop,	"Chop ESI fetch to bits")
DEBUG_BIT(FLUSH_HEAD,		flush_head,	"Flush after http1 head")
DEBUG_BIT(VTC_MODE,		vtc_mode,	"Varnishtest Mode")
DEBUG_BIT(WITNESS,		witness,	"Emit WITNESS lock records")
DEBUG_BIT(VSM_KEEP,		vsm_keep,	"Keep the VSM file on restart")
DEBUG_BIT(DROP_POOLS,		drop_pools,	"Drop thread pools (testing)")
DEBUG_BIT(SLOW_ACCEPTOR,	slow_acceptor,	"Slow down Acceptor")
DEBUG_BIT(H2_NOCHECK,		h2_nocheck,	"Disable various H2 checks")
DEBUG_BIT(VMOD_SO_KEEP,		vmod_so_keep,	"Keep copied VMOD libraries")
DEBUG_BIT(PROCESSORS,		processors,	"Fetch/Deliver processors")
DEBUG_BIT(PROTOCOL,		protocol,	"Protocol debugging")
DEBUG_BIT(VCL_KEEP,		vcl_keep,	"Keep VCL C and so files")
#undef DEBUG_BIT

/*lint -restore */
