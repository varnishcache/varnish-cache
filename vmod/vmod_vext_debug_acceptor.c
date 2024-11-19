/*-
 * Copyright 2024 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Author: Nils Goroll <nils.goroll@uplex.de>
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
 */

#include "config.h"

#include <stdio.h>

#include "cache/cache_varnishd.h"
#include "acceptor/cache_acceptor.h"
#include "acceptor/mgt_acceptor.h"

struct acceptor DBG_acceptor;

// these callbacks are all noops, because we initialize our dbg acceptor as a
// tcp acceptor, which keeps a list of its instances, so the respective
// functions get already called through the tcp acceptor

static int
acc_dbg_reopen(void)
{
	return (0);
}

static void
acc_dbg_start(struct cli *cli)
{
	(void) cli;
}

static void
acc_dbg_accept(struct pool * pp)
{
	(void) pp;
}

static void
acc_dbg_update(pthread_mutex_t * shut_mtx)
{
	(void) shut_mtx;
}

static void
acc_dbg_shutdown(void)
{
}

static void __attribute__((constructor))
init_register_acceptor(void) {
    fprintf(stderr, "HELLO from dbg acceptor VEXT\n");
    DBG_acceptor = TCP_acceptor;
    DBG_acceptor.name = "dbg";
    DBG_acceptor.reopen = acc_dbg_reopen;
    DBG_acceptor.start = acc_dbg_start;
    DBG_acceptor.accept = acc_dbg_accept;
    DBG_acceptor.update = acc_dbg_update;
    DBG_acceptor.shutdown = acc_dbg_shutdown;

    VCA_Add(&DBG_acceptor);
}
