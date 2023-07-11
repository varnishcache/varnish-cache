/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 */

#include "config.h"

#include "cache_varnishd.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_SIGALTSTACK
#  include <sys/mman.h>
#endif

#ifdef HAVE_PTHREAD_NP_H
#  include <pthread_np.h>
#endif

#include "common/heritage.h"

#include "vcli_serve.h"
#include "vnum.h"
#include "vtim.h"
#include "vrnd.h"

#include "hash/hash_slinger.h"

volatile struct params		*cache_param;
static pthread_mutex_t		cache_vrnd_mtx;
static vtim_dur			shutdown_delay = 0;

pthread_mutexattr_t mtxattr_errorcheck;

static void
cache_vrnd_lock(void)
{
	PTOK(pthread_mutex_lock(&cache_vrnd_mtx));
}

static void
cache_vrnd_unlock(void)
{
	PTOK(pthread_mutex_unlock(&cache_vrnd_mtx));
}

/*--------------------------------------------------------------------
 * Per thread storage for the session currently being processed by
 * the thread.  This is used for panic messages.
 */

static pthread_key_t req_key;
static pthread_key_t bo_key;
static pthread_key_t wrk_key;
pthread_key_t witness_key;

void
THR_SetBusyobj(const struct busyobj *bo)
{

	PTOK(pthread_setspecific(bo_key, bo));
}

struct busyobj *
THR_GetBusyobj(void)
{

	return (pthread_getspecific(bo_key));
}

void
THR_SetRequest(const struct req *req)
{

	PTOK(pthread_setspecific(req_key, req));
}

struct req *
THR_GetRequest(void)
{

	return (pthread_getspecific(req_key));
}

void
THR_SetWorker(const struct worker *wrk)
{

	PTOK(pthread_setspecific(wrk_key, wrk));
}

struct worker *
THR_GetWorker(void)
{

	return (pthread_getspecific(wrk_key));
}

/*--------------------------------------------------------------------
 * Name threads if our pthreads implementation supports it.
 */

static pthread_key_t name_key;

void
THR_SetName(const char *name)
{

	PTOK(pthread_setspecific(name_key, name));
#if defined(HAVE_PTHREAD_SET_NAME_NP)
	pthread_set_name_np(pthread_self(), name);
#elif defined(HAVE_PTHREAD_SETNAME_NP)
#if defined(__APPLE__)
	(void)pthread_setname_np(name);
#elif defined(__NetBSD__)
	(void)pthread_setname_np(pthread_self(), "%s", (char *)(uintptr_t)name);
#else
	(void)pthread_setname_np(pthread_self(), name);
#endif
#endif
}

const char *
THR_GetName(void)
{

	return (pthread_getspecific(name_key));
}

/*--------------------------------------------------------------------
 * Generic setup all our threads should call
 */
#ifdef HAVE_SIGALTSTACK
static stack_t altstack;
#endif

void
THR_Init(void)
{
#ifdef HAVE_SIGALTSTACK
	if (altstack.ss_sp != NULL)
		AZ(sigaltstack(&altstack, NULL));
#endif
}

/*--------------------------------------------------------------------
 * VXID's are unique transaction numbers allocated with a minimum of
 * locking overhead via pools in the worker threads.
 *
 * VXID's are mostly for use in VSL and for that reason we never return
 * zero vxid, in order to reserve that for "unassociated" VSL records.
 */

static uint64_t vxid_base = 1;
static uint32_t vxid_chunk = 32768;
static struct lock vxid_lock;

vxid_t
VXID_Get(const struct worker *wrk, uint64_t mask)
{
	struct vxid_pool *v;
	vxid_t retval;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(wrk->wpriv, WORKER_PRIV_MAGIC);
	v = wrk->wpriv->vxid_pool;
	AZ(mask & VSL_IDENTMASK);
	while (v->count == 0 || v->next >= VRT_INTEGER_MAX) {
		Lck_Lock(&vxid_lock);
		v->next = vxid_base;
		v->count = vxid_chunk;
		vxid_base += v->count;
		if (vxid_base >= VRT_INTEGER_MAX)
			vxid_base = 1;
		Lck_Unlock(&vxid_lock);
	}
	v->count--;
	assert(v->next > 0);
	assert(v->next < VSL_CLIENTMARKER);
	retval.vxid = v->next | mask;
	v->next++;
	return (retval);
}

/*--------------------------------------------------------------------
 * Debugging aids
 */

/*
 * Dumb down the VXID allocation to make it predictable for
 * varnishtest cases
 */
static void v_matchproto_(cli_func_t)
cli_debug_xid(struct cli *cli, const char * const *av, void *priv)
{
	(void)priv;
	if (av[2] != NULL) {
		vxid_base = strtoull(av[2], NULL, 0);
		vxid_chunk = 0;
		if (av[3] != NULL)
			vxid_chunk = strtoul(av[3], NULL, 0);
		if (vxid_chunk == 0)
			vxid_chunk = 1;
	}
	VCLI_Out(cli, "XID is %ju chunk %u", (uintmax_t)vxid_base, vxid_chunk);
}

/*
 * Artificially slow down the process shutdown.
 */
static void v_matchproto_(cli_func_t)
cli_debug_shutdown_delay(struct cli *cli, const char * const *av, void *priv)
{

	(void)cli;
	(void)priv;
	shutdown_delay = VNUM_duration(av[2]);
}

/*
 * Default to seed=1, this is the only seed value POSIXl guarantees will
 * result in a reproducible random number sequence.
 */
static void v_matchproto_(cli_func_t)
cli_debug_srandom(struct cli *cli, const char * const *av, void *priv)
{
	unsigned seed = 1;

	(void)priv;
	(void)cli;
	if (av[2] != NULL)
		seed = strtoul(av[2], NULL, 0);
	VRND_SeedTestable(seed);
}

static struct cli_proto debug_cmds[] = {
	{ CLICMD_DEBUG_XID,		"d", cli_debug_xid },
	{ CLICMD_DEBUG_SHUTDOWN_DELAY,	"d", cli_debug_shutdown_delay },
	{ CLICMD_DEBUG_SRANDOM,		"d", cli_debug_srandom },
	{ NULL }
};

/*--------------------------------------------------------------------
 * XXX: Think more about which order we start things
 */

#if defined(__FreeBSD__) && __FreeBSD_version >= 1000000
static void
child_malloc_fail(void *p, const char *s)
{
	VSL(SLT_Error, NO_VXID, "MALLOC ERROR: %s (%p)", s, p);
	fprintf(stderr, "MALLOC ERROR: %s (%p)\n", s, p);
	WRONG("Malloc Error");
}
#endif

/*=====================================================================
 * signal handler for child process
 */

static void v_noreturn_ v_matchproto_()
child_signal_handler(int s, siginfo_t *si, void *c)
{
	char buf[1024];
	struct sigaction sa;
	struct req *req;
	const char *a, *p, *info = NULL;

	(void)c;
	/* Don't come back */
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = SIG_DFL;
	(void)sigaction(SIGSEGV, &sa, NULL);
	(void)sigaction(SIGBUS, &sa, NULL);
	(void)sigaction(SIGABRT, &sa, NULL);

	while (s == SIGSEGV || s == SIGBUS) {
		req = THR_GetRequest();
		if (req == NULL || req->wrk == NULL)
			break;
		a = TRUST_ME(si->si_addr);
		p = TRUST_ME(req->wrk);
		p += sizeof *req->wrk;
		// rough safe estimate - top of stack
		if (a > p + cache_param->wthread_stacksize)
			break;
		if (a < p - 2 * cache_param->wthread_stacksize)
			break;
		info = "\nTHIS PROBABLY IS A STACK OVERFLOW - "
			"check thread_pool_stack parameter";
		break;
	}
	bprintf(buf, "Signal %d (%s) received at %p si_code %d%s",
		s, strsignal(s), si->si_addr, si->si_code,
		info ? info : "");

	VAS_Fail(__func__,
		 __FILE__,
		 __LINE__,
		 buf,
		 VAS_WRONG);
}

/*=====================================================================
 * Magic for panicking properly on signals
 */

static void
child_sigmagic(size_t altstksz)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof sa);

#ifdef HAVE_SIGALTSTACK
	size_t sz = vmax_t(size_t, SIGSTKSZ + 4096, altstksz);
	altstack.ss_sp = mmap(NULL, sz,  PROT_READ | PROT_WRITE,
			      MAP_PRIVATE | MAP_ANONYMOUS,
			      -1, 0);
	AN(altstack.ss_sp != MAP_FAILED);
	AN(altstack.ss_sp);
	altstack.ss_size = sz;
	altstack.ss_flags = 0;
	sa.sa_flags |= SA_ONSTACK;
#else
	(void)altstksz;
#endif

	THR_Init();

	sa.sa_sigaction = child_signal_handler;
	sa.sa_flags |= SA_SIGINFO;
	(void)sigaction(SIGBUS, &sa, NULL);
	(void)sigaction(SIGABRT, &sa, NULL);
	(void)sigaction(SIGSEGV, &sa, NULL);
}

static void
cli_quit(int sig)
{

	if (!IS_CLI()) {
		PTOK(pthread_kill(cli_thread, sig));
		return;
	}

	WRONG("It's time for the big quit");
}

static void
child_sig_nore(int sig)
{

	(void)sig;
}

/*=====================================================================
 * Run the child process
 */

void
child_main(int sigmagic, size_t altstksz)
{

	if (sigmagic)
		child_sigmagic(altstksz);
	(void)signal(SIGINT, SIG_DFL);
	(void)signal(SIGTERM, SIG_DFL);
	(void)signal(SIGQUIT, cli_quit);
	(void)signal(SIGUSR1, child_sig_nore);

#if defined(__FreeBSD__) && __FreeBSD_version >= 1000000
	malloc_message = child_malloc_fail;
#endif

	/* Before anything uses pthreads in anger */
	PTOK(pthread_mutexattr_init(&mtxattr_errorcheck));
	PTOK(pthread_mutexattr_settype(&mtxattr_errorcheck, PTHREAD_MUTEX_ERRORCHECK));

	cache_param = heritage.param;

	PTOK(pthread_key_create(&req_key, NULL));
	PTOK(pthread_key_create(&bo_key, NULL));
	PTOK(pthread_key_create(&wrk_key, NULL));
	PTOK(pthread_key_create(&witness_key, free));
	PTOK(pthread_key_create(&name_key, NULL));

	THR_SetName("cache-main");

	PTOK(pthread_mutex_init(&cache_vrnd_mtx, &mtxattr_errorcheck));
	VRND_Lock = cache_vrnd_lock;
	VRND_Unlock = cache_vrnd_unlock;

	VSM_Init();	/* First, LCK needs it. */

	LCK_Init();	/* Second, locking */

	Lck_New(&vxid_lock, lck_vxid);

	CLI_Init();
	PAN_Init();
	VFP_Init();

	ObjInit();

	WRK_Init();

	VCL_Init();
	VCL_VRT_Init();

	HTTP_Init();

	VBO_Init();
	VCP_Init();
	VBP_Init();
	VDI_Init();
	VBE_InitCfg();
	Pool_Init();
	V1P_Init();
	V2D_Init();

	EXP_Init();
	HSH_Init(heritage.hash);
	BAN_Init();

	VCA_Init();

	SMUG_Init();

	STV_open();

	VMOD_Init();

	BAN_Compile();

	VRND_SeedAll();


	CLI_AddFuncs(debug_cmds);

#if WITH_PERSISTENT_STORAGE
	/* Wait for persistent storage to load if asked to */
	if (FEATURE(FEATURE_WAIT_SILO))
		SMP_Ready();
#endif

	CLI_Run();

	if (shutdown_delay > 0)
		VTIM_sleep(shutdown_delay);

	VCA_Shutdown();
	BAN_Shutdown();
	EXP_Shutdown();
	STV_close();

	printf("Child dies\n");
}
