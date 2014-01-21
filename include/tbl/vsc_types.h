/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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
 * Fields (n, l, e, d):
 *    n - Name:		Field name, in C-source
 *    t - Type:		Type name, in shm chunk
 *    l - Label:	Display name, in stats programs
 *    e - Explanation:	Short description of this counter type
 *    d - Description:	Long description of this counter type
 *
 * The order in which the types are defined in this file determines the
 * order in which counters are reported in the API, and then also the
 * display order in varnishstat.
 */

/*lint -save -e525 -e539 */
VSC_TYPE_F(main,	"MAIN",		"",		"Child",
    "Child process main counters"
)
VSC_TYPE_F(mgt,		"MGT",		"MGT",		"Master",
    "Management process counters"
)
VSC_TYPE_F(mempool,	"MEMPOOL",	"MEMPOOL",	"Memory pool",
    "Memory pool counters"
)
VSC_TYPE_F(sma,		"SMA",		"SMA",		"Storage malloc",
    "Malloc storage counters"
)
VSC_TYPE_F(smf,		"SMF",		"SMF",		"Storage file",
    "File storage counters"
)
VSC_TYPE_F(vbe,		"VBE",		"VBE",		"Backend",
    "Backend counters"
)
VSC_TYPE_F(lck,		"LCK",		"LCK",		"Lock",
    "Mutex lock counters"
)
/*lint -restore */
