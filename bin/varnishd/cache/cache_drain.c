/*-
 * Copyright (c) 2026 Varnish Software AS
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Connection draining support for graceful shutdown
 *
 * When draining is started, Varnish stops accepting new connections
 * and adds "Connection: close" headers to responses.  The child process
 * monitors active sessions and exits when all sessions have closed or
 * the timeout expires.
 */

#include "config.h"

#include "cache_varnishd.h"
#include "acceptor/cache_acceptor.h"
#include "vcli_serve.h"
#include "vtim.h"
#include "vnum.h"

static struct lock	drain_mtx;
static int		draining = 0;
static vtim_real	drain_deadline = 0.0;
static volatile long	n_active_sess = 0;
static volatile int	drain_exiting = 0;

/*--------------------------------------------------------------------
 * Signal drain completion by waking the CLI thread.
 * This causes CLI_Run() to return, triggering clean child shutdown.
 * Uses atomic compare-and-swap to ensure we only signal once.
 */

static void
drain_signal_exit(void)
{

	if (__sync_bool_compare_and_swap(&drain_exiting, 0, 1))
		CLI_Wakeup();
}

/*--------------------------------------------------------------------
 * Monitor thread that waits for drain timeout or session completion.
 */

static void *
drain_monitor(void *arg)
{

	(void)arg;

	while (VTIM_real() < drain_deadline) {
		if (n_active_sess <= 0)
			drain_signal_exit();
		VTIM_sleep(0.1);
	}

	/* Timeout expired */
	drain_signal_exit();
	return (NULL);
}

/*--------------------------------------------------------------------*/

int
DRAIN_Active(void)
{
	int r;

	Lck_Lock(&drain_mtx);
	r = draining;
	Lck_Unlock(&drain_mtx);
	return (r);
}

void
DRAIN_IncSess(void)
{

	(void)__sync_fetch_and_add(&n_active_sess, 1);
}

void
DRAIN_DecSess(void)
{
	long n;

	n = __sync_sub_and_fetch(&n_active_sess, 1);

	/* If draining and no more sessions, exit immediately */
	if (n <= 0 && DRAIN_Active())
		drain_signal_exit();
}

void
DRAIN_Start(vtim_dur timeout)
{
	pthread_t thr;

	Lck_Lock(&drain_mtx);
	if (draining) {
		Lck_Unlock(&drain_mtx);
		return;
	}
	draining = 1;
	drain_deadline = VTIM_real() + timeout;
	Lck_Unlock(&drain_mtx);

	/* Stop accepting new connections */
	VCA_Shutdown();

	/* Wake idle workers to release VCL references faster */
	Pool_WakeIdle();

	/* Start monitor thread for timeout */
	PTOK(pthread_create(&thr, NULL, drain_monitor, NULL));
}

static void v_matchproto_(cli_func_t)
cli_drain(struct cli *cli, const char * const *av, void *priv)
{
	vtim_dur timeout;

	(void)priv;

	timeout = VNUM_duration(av[2]);
	if (isnan(timeout) || timeout < 0) {
		VCLI_SetResult(cli, CLIS_PARAM);
		VCLI_Out(cli, "Invalid timeout");
		return;
	}

	DRAIN_Start(timeout);
	VCLI_Out(cli, "Draining started");
}

static struct cli_proto drain_cmds[] = {
	{ CLICMD_DEBUG_DRAIN, "d", cli_drain },
	{ NULL }
};

void
DRAIN_Init(void)
{

	Lck_New(&drain_mtx, lck_drain);
	CLI_AddFuncs(drain_cmds);
}
