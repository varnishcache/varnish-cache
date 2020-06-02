/*-
 * Copyright (c) 2006-2011 Varnish Software AS
 * Copyright 2011-2020 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *	   Nils Goroll <nils.goroll@uplex.de>
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
 * "Jailing" *1) child processes on Solaris and Solaris-derivatives *2)
 * ====================================================================
 *
 * *1) The name is motivated by the availability of the -j command line
 *     option. Jailing Varnish is not to be confused with BSD Jails or
 *     Solaris Zones.
 *
 *     In Solaris parlour, jail == least privileges
 *
 * *2) e.g. illumos, SmartOS, OmniOS etc.
 *
 *
 * Note on use of symbolic PRIV_* constants
 * ----------------------------------------
 *
 * We assume backwards compatibility only for Solaris Releases after the
 * OpenSolaris Launch. For privileges which existed at the time of the
 * OpenSolaris Launch, we use the constants from sys/priv_names.h and assert
 * that priv_addset must succeed.
 *
 * For privileges which have been added later, we need to use priv strings in
 * order not to break builds of varnish on older platforms. To remain binary
 * compatible, we can't assert that priv_addset succeeds, but we may assert that
 * it either succeeds or fails with EINVAL.
 *
 * See priv_setop_check()
 *
 * Note on introduction of new privileges (or: lack of forward compatibility)
 * --------------------------------------------------------------------------
 *
 * For optimal build and binary forward compatibility, we could use subtractive
 * set specs like
 *
 *       basic,!file_link_any,!proc_exec,!proc_fork,!proc_info,!proc_session
 *
 * which would implicitly keep any privileges newly introduced to the 'basic'
 * set.
 *
 * But we have a preference for making an informed decision about which
 * privileges varnish subprocesses should have, so we prefer to risk breaking
 * varnish temporarily on newer kernels and be notified of missing privileges
 * through bug reports.
 *
 * Notes on the SNOCD flag
 * -----------------------
 *
 * On Solaris, any uid/gid fiddling which can be interpreted as 'waiving
 * privileges' will lead to the processes' SNOCD flag being set, disabling core
 * dumps unless explicitly allowed using coreadm (see below). There is no
 * equivalent to Linux PR_SET_DUMPABLE. The only way to clear the flag is a call
 * to some form of exec(). The presence of the SNOCD flag also prevents many
 * process manipulations from other processes with the same uid/gid unless the
 * latter have the proc_owner privilege.
 *
 * Thus, if we want to run subprocesses with a different uid/gid than the master
 * process, we cannot avoid the SNOCD flag for those subprocesses not exec'ing
 * (VCC, VCLLOAD, WORKER).
 *
 *
 * We should, however, avoid to accidentally set the SNOCD flag when setting
 * privileges (see https://www.varnish-cache.org/trac/ticket/671 )
 *
 * When changing the logic herein, always check with mdb -k. Replace _PID_ with
 * the pid of your varnish child, the result should be 0, otherwise a regression
 * has been introduced.
 *
 * > 0t_PID_::pid2proc | ::print proc_t p_flag | >a
 * > (<a & 0x10000000)=X
 *		0
 *
 * (a value of 0x10000000 indicates that SNOCD is set)
 *
 * How to get core dumps of the worker process on Solaris
 * ------------------------------------------------------
 *
 * (see previous paragraph for explanation).
 *
 * Two options:
 *
 * - start the varnish master process under the same user/group given for the -u
 *   / -g command line option and elevated privileges but without proc_setid,
 *   e.g.:
 *
 *	pfexec ppriv -e -s A=basic,net_privaddr,sys_resource varnishd ...
 *
 * - allow coredumps of setid processes (ignoring SNOCD)
 *
 *   See coreadm(1M) - global-setid / proc-setid
 *
 * brief history of privileges introduced since OpenSolaris Launch
 * ---------------------------------------------------------------
 *
 * (from hg log -gp usr/src/uts/common/os/priv_defs
 *    or git log -p usr/src/uts/common/os/priv_defs)
 *
 * ARC cases are not necessarily accurate (induced from commit msg)
 *
 * privileges used here marked with *
 *
 * Illumos ticket
 * ARC case	    hg/git commit  first release
 *
 * PSARC/2006/155?  37f4a3e2bd99   onnv_37
 * - file_downgrade_sl
 * - file_upgrade_sl
 * - net_bindmlp
 * - net_mac_aware
 * - sys_trans_label
 * - win_colormap
 * - win_config
 * - win_dac_read
 * - win_dac_write
 * - win_devices
 * - win_dga
 * - win_downgrade_sl
 * - win_fontpath
 * - win_mac_read
 * - win_mac_write
 * - win_selection
 * - win_upgrade_sl
 *
 * PSARC/2006/218   5dbf296c1e57   onnv_39
 * - graphics_access
 * - graphics_map
 *
 * PSARC/2006/366   aaf16568054b   onnv_57
 * - net_config
 *
 * PSARC/2007/315?  3047ad28a67b   onnv_77
 * - file_flag_set
 *
 * PSARC/2007/560?  3047ad28a67b   onnv_77
 * - sys_smb
 *
 * PSARC 2008/046   47f6aa7a8077   onnv_85
 * - contract_identify
 *
 * PSARC 2008/289   79a9dac325d9   onnv_92
 * - virt_manage
 * - xvm_control
 *
 * PSARC 2008/473   eff7960d93cd   onnv_98
 * - sys_dl_config
 *
 * PSARC/2006/475   faf256d5c16c   onnv_103
 * - net_observability
 *
 * PSARC/2009/317   8e29565352fc   onnv_117
 * - sys_ppp_config
 *
 * PSARC/2009/373   3be00c4a6835   onnv_125
 * - sys_iptun_config
 *
 * PSARC/2008/252   e209937a4f19   onnv_128
 * - net_mac_implicit
 *
 * PSARC/2009/685   8eca52188202   onnv_132
 * * net_access
 *
 * PSARC/2009/378   63678502e95e   onnv_140
 * * file_read
 * * file_write
 *
 * PSARC/2010/181   15439b11d535   onnv_142
 * - sys_res_bind
 *
 * unknown	    unknown	   Solaris11
 * - sys_flow_config
 * - sys_share
 *
 * IL3923	    24d819e6779c   Illumos
 * - proc_prioup
 *
 */

//lint -e{766}
#include "config.h"

#ifdef HAVE_SETPPRIV

#include <stdio.h>	// ARG_ERR
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mgt/mgt.h"

#include "common/heritage.h"

#ifdef HAVE_PRIV_H
#include <priv.h>
#endif

/* renamed from sys/priv_const.h */
#define VJS_EFFECTIVE		0
#define VJS_INHERITABLE		1
#define VJS_PERMITTED		2
#define VJS_LIMIT		3

#define VJS_NSET		(VJS_LIMIT + 1)

#define VJS_MASK(x) (1U << (x))

/* to denote sharing */
#define JAIL_MASTER_ANY 0

const priv_ptype_t vjs_ptype[VJS_NSET] = {
	[VJS_EFFECTIVE]		= PRIV_EFFECTIVE,
	[VJS_INHERITABLE]	= PRIV_INHERITABLE,
	[VJS_PERMITTED]		= PRIV_PERMITTED,
	[VJS_LIMIT]		= PRIV_LIMIT
};

static priv_set_t *vjs_sets[JAIL_LIMIT][VJS_NSET];
static priv_set_t *vjs_inverse[JAIL_LIMIT][VJS_NSET];
static priv_set_t *vjs_proc_setid;	// for vjs_setuid

static void v_matchproto_(jail_master_f)
	vjs_master(enum jail_master_e jme);

/*------------------------------------------------------------*/

static inline int
priv_setop_check(int a)
{
	if (a == 0)
		return (1);
	if (errno == EINVAL)
		return (1);
	return (0);
}

#define priv_setop_assert(a) assert(priv_setop_check(a))

/*------------------------------------------------------------*/

static int
vjs_priv_on(int vs, priv_set_t **set)
{
	assert(vs >= 0);
	assert(vs < VJS_NSET);

	return (setppriv(PRIV_ON, vjs_ptype[vs], set[vs]));
}

/* ------------------------------------------------------------
 * initialization of privilege sets from mgt_jail_solaris_tbl.h
 * and implicit rules documented therein
 */

static inline void
vjs_add(priv_set_t *sets[VJS_NSET], unsigned mask, const char *priv)
{
	int i;
	for (i = 0; i < VJS_NSET; i++)
		if (mask & VJS_MASK(i))
			priv_setop_assert(priv_addset(sets[i], priv));
}

/* add SUBPROC INHERITABLE and PERMITTED to MASTER PERMITTED */
static int
vjs_master_rules(void)
{
	priv_set_t *punion = priv_allocset();
	int vs, vj;

	AN(punion);

	for (vs = VJS_INHERITABLE; vs <= VJS_PERMITTED; vs ++) {
		priv_emptyset(punion);
		for (vj = JAIL_SUBPROC; vj < JAIL_LIMIT; vj++)
			priv_union(vjs_sets[vj][vs], punion);
		priv_union(punion, vjs_sets[JAIL_MASTER_ANY][VJS_PERMITTED]);
	}

	priv_freeset(punion);

	return (0);
}

static priv_set_t *
vjs_alloc(void)
{
	priv_set_t *s;

	s = priv_allocset();
	AN(s);
	priv_emptyset(s);
	return (s);
}

static int v_matchproto_(jail_init_f)
vjs_init(char **args)
{
	priv_set_t **sets, *permitted, *inheritable, *user = NULL;
	const char *e;
	int vj, vs;

	if (args != NULL && *args != NULL) {
		for (;*args != NULL; args++) {
			if (!strncmp(*args, "worker=", 7)) {
				user = priv_str_to_set((*args) + 7, ",", &e);
				if (user == NULL)
					ARGV_ERR(
					    "-jsolaris: parsing worker= "
					    "argument failed near %s.\n",
					    e);
				continue;
			}
			ARGV_ERR("-jsolrais: unknown sub-argument '%s'\n",
			    *args);
		}
	}

	permitted = vjs_alloc();
	AN(permitted);
	AZ(getppriv(PRIV_PERMITTED, permitted));

	inheritable = vjs_alloc();
	AN(inheritable);
	AZ(getppriv(PRIV_INHERITABLE, inheritable));
	priv_union(permitted, inheritable);

	/* init privset for vjs_setuid() */
	vjs_proc_setid = vjs_alloc();
	AN(vjs_proc_setid);
	priv_setop_assert(priv_addset(vjs_proc_setid, PRIV_PROC_SETID));

	assert(JAIL_MASTER_ANY < JAIL_SUBPROC);
	/* alloc privsets.
	 * for master, PERMITTED and LIMIT are shared
	 */
	for (vj = 0; vj < JAIL_SUBPROC; vj++)
		for (vs = 0; vs < VJS_NSET; vs++) {
			if (vj == JAIL_MASTER_ANY || vs < VJS_PERMITTED) {
				vjs_sets[vj][vs] = vjs_alloc();
				vjs_inverse[vj][vs] = vjs_alloc();
			} else {
				vjs_sets[vj][vs] =
					vjs_sets[JAIL_MASTER_ANY][vs];
				vjs_inverse[vj][vs] =
					vjs_inverse[JAIL_MASTER_ANY][vs];
			}
		}

	for (; vj < JAIL_LIMIT; vj++)
		for (vs = 0; vs < VJS_NSET; vs++) {
			vjs_sets[vj][vs] = vjs_alloc();
			vjs_inverse[vj][vs] = vjs_alloc();
		}

	/* init from table */
#define PRIV(name, mask, priv) vjs_add(vjs_sets[JAIL_ ## name], mask, priv);
#include "mgt_jail_solaris_tbl.h"

	if (user != NULL)
		priv_union(user, vjs_sets[JAIL_SUBPROC_WORKER][VJS_EFFECTIVE]);

	/* mask by available privs */
	for (vj = 0; vj < JAIL_LIMIT; vj++) {
		sets = vjs_sets[vj];
		priv_intersect(permitted, sets[VJS_EFFECTIVE]);
		priv_intersect(permitted, sets[VJS_PERMITTED]);
		priv_intersect(inheritable, sets[VJS_INHERITABLE]);
	}

	/* SUBPROC implicit rules */
	for (vj = JAIL_SUBPROC; vj < JAIL_LIMIT; vj++) {
		sets = vjs_sets[vj];
		priv_union(sets[VJS_EFFECTIVE], sets[VJS_PERMITTED]);
		priv_union(sets[VJS_PERMITTED], sets[VJS_LIMIT]);
		priv_union(sets[VJS_INHERITABLE], sets[VJS_LIMIT]);
	}

	vjs_master_rules();

	/* MASTER implicit rules */
	for (vj = 0; vj < JAIL_SUBPROC; vj++) {
		sets = vjs_sets[vj];
		priv_union(sets[VJS_EFFECTIVE], sets[VJS_PERMITTED]);
		priv_union(sets[VJS_PERMITTED], sets[VJS_LIMIT]);
		priv_union(sets[VJS_INHERITABLE], sets[VJS_LIMIT]);
	}

	/* generate inverse */
	for (vj = 0; vj < JAIL_LIMIT; vj++)
		for (vs = 0; vs < VJS_NSET; vs++) {
			priv_copyset(vjs_sets[vj][vs], vjs_inverse[vj][vs]);
			priv_inverse(vjs_inverse[vj][vs]);
		}

	vjs_master(JAIL_MASTER_LOW);

	priv_freeset(permitted);
	priv_freeset(inheritable);
	/* XXX LEAK: no _fini for priv_freeset() */
	return (0);
}

static void
vjs_waive(int jail)
{
	priv_set_t **sets;
	int i;

	assert(jail >= 0);
	assert(jail < JAIL_LIMIT);

	sets = vjs_inverse[jail];

	for (i = 0; i < VJS_NSET; i++)
		AZ(setppriv(PRIV_OFF, vjs_ptype[i], sets[i]));
}

static void
vjs_setuid(void)
{
	if (priv_ineffect(PRIV_PROC_SETID)) {
		if (getgid() != mgt_param.gid)
			XXXAZ(setgid(mgt_param.gid));
		if (getuid() != mgt_param.uid)
			XXXAZ(setuid(mgt_param.uid));
		AZ(setppriv(PRIV_OFF, PRIV_EFFECTIVE, vjs_proc_setid));
		AZ(setppriv(PRIV_OFF, PRIV_PERMITTED, vjs_proc_setid));
	} else {
		MGT_Complain(C_SECURITY,
		    "Privilege %s missing, will not change uid/gid",
		    PRIV_PROC_SETID);
	}
}

static void v_matchproto_(jail_subproc_f)
vjs_subproc(enum jail_subproc_e jse)
{

	AZ(vjs_priv_on(VJS_EFFECTIVE, vjs_sets[jse]));
	AZ(vjs_priv_on(VJS_INHERITABLE, vjs_sets[jse]));

	vjs_setuid();
	vjs_waive(jse);
}

static void v_matchproto_(jail_master_f)
vjs_master(enum jail_master_e jme)
{

	assert(jme < JAIL_SUBPROC);

	AZ(vjs_priv_on(VJS_EFFECTIVE, vjs_sets[jme]));
	AZ(vjs_priv_on(VJS_INHERITABLE, vjs_sets[jme]));

	vjs_waive(jme);
}

const struct jail_tech jail_tech_solaris = {
	.magic =	JAIL_TECH_MAGIC,
	.name =		"solaris",
	.init =		vjs_init,
	.master =	vjs_master,
//	.make_workdir =	vjs_make_workdir,
//	.storage_file =	vjs_storage_file,
	.subproc =	vjs_subproc,
};

#endif /* HAVE_SETPPRIV */
