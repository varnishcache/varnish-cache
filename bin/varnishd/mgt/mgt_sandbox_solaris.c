/*-
 * Copyright (c) 2006-2011 Varnish Software AS
 * Copyright (c) 2011-2012 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *	   Nils Goroll <nils.goroll@uplex.de>
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
 * Sandboxing child processes on Solaris
 *
 */

#include "config.h"

#ifdef HAVE_SETPPRIV

#ifdef HAVE_PRIV_H
#include <priv.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "mgt/mgt.h"

#include "common/heritage.h"
#include "common/params.h"

/*--------------------------------------------------------------------
 * SOLARIS PRIVILEGES: Note on use of symbolic PRIV_* constants
 *
 * We assume backwards compatibility only for Solaris Releases after the
 * OpenSolaris Launch. For privileges which existed at the time of the
 * OpenSolaris Launch, we use the constants from sys/priv_names.h and assert
 * that priv_addset must succeed.
 *
 * For privileges which have been added later, we need to use priv strings in
 * order not to break builds of varnish on these platforms. To remain binary
 * compatible, we can't assert that priv_addset succeeds, but we may assert that
 * it either succeeds or fails with EINVAL.
 */

/* for priv_delset() and priv_addset() */
static inline int
priv_setop_check(int a) {
	if (a == 0)
		return (1);
	if (errno == EINVAL)
		return (1);
	return (0);
}

#define priv_setop_assert(a) assert(priv_setop_check(a))

/*
 * we try to add all possible privileges to waive them later.
 *
 * when doing so, we need to expect EPERM
 */

/* for setppriv */
static inline int
setppriv_check(int a) {
	if (a == 0)
		return (1);
	if (errno == EPERM)
		return (1);
	return (0);
}

#define setppriv_assert(a) assert(setppriv_check(a))


/*
 * brief histroy of introduction of privileges since OpenSolaris Launch
 *
 * (from hg log -gp usr/src/uts/common/os/priv_defs)
 *
 * ARC cases are not necessarily accurate (induced from commit msg)
 * (marked with ?)
 *
 * privileges used here marked with *
 *
 *
 * ARC case	    hg commit	   first release
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
 *
 * SOLARIS PRIVILEGES: Note on introtiction of new privileges (forward
 *		       compatibility)
 *
 * For optimal build and binary forward comatibility, we could use subtractive
 * set specs like
 *
 *       basic,!file_link_any,!proc_exec,!proc_fork,!proc_info,!proc_session
 *
 * but I (Nils) have a preference for making an informed decision about which
 * privileges the varnish child should have and which it shouldn't.
 *
 * Newly introduced privileges should be annotated with their PSARC / commit ID
 * (as long as Oracle reveils these :/ )
 *
 * SOLARIS PRIVILEGES: Note on accidentally setting the SNOCD flag
 *
 * When setting privileges, we need to take care not to accidentally set the
 * SNOCD flag which will disable core dumps unnecessarily. (see
 * https://www.varnish-cache.org/trac/ticket/671 )
 *
 * When changing the logic herein, always check with mdb -k. Replace _PID_ with
 * the pid of your varnish child, the result should be 0, otherwise a regression
 * has been introduced.
 *
 * > 0t_PID_::pid2proc | ::print proc_t p_flag | >a
 * > (<a & 0x10000000)=X
 *                 0
 *
 * (a value of 0x10000000 indicates that SNOCD is set)
 *
 * NOTE that on Solaris changing the uid will _always_ set SNOCD, so make sure
 * you run this test with appropriate privileges, but without proc_setid, so
 * varnish won't setuid(), e.g.
 *
 * pfexec ppriv -e -s A=basic,net_privaddr,sys_resource varnish ...
 *
 * SOLARIS COREDUMPS with setuid(): See coreadm(1M) - global-setid / proc-setid
 *
 */

static void
mgt_sandbox_solaris_add_inheritable(priv_set_t *pset, enum sandbox_e who)
{
	switch (who) {
	case SANDBOX_VCC:
		/* for /etc/resolv.conf and /etc/hosts */
		priv_setop_assert(priv_addset(pset, "file_read"));
		break;
	case SANDBOX_CC:
		priv_setop_assert(priv_addset(pset, PRIV_PROC_EXEC));
		priv_setop_assert(priv_addset(pset, PRIV_PROC_FORK));
		priv_setop_assert(priv_addset(pset, "file_read"));
		priv_setop_assert(priv_addset(pset, "file_write"));
		break;
	case SANDBOX_VCLLOAD:
		break;
	case SANDBOX_WORKER:
		break;
	default:
		REPORT(LOG_ERR, "INCOMPLETE AT: %s(%d)\n", __func__, __LINE__);
		exit(1);
	}
}

/*
 * effective is initialized from inheritable (see mgt_sandbox_solaris_waive)
 * so only additionally required privileges need to be added here
 */

static void
mgt_sandbox_solaris_add_effective(priv_set_t *pset, enum sandbox_e who)
{
	switch (who) {
	case SANDBOX_VCC:
		priv_setop_assert(priv_addset(pset, "file_write"));
		break;
	case SANDBOX_CC:
		break;
	case SANDBOX_VCLLOAD:
		priv_setop_assert(priv_addset(pset, "file_read"));
	case SANDBOX_WORKER:
		priv_setop_assert(priv_addset(pset, "net_access"));
		priv_setop_assert(priv_addset(pset, "file_read"));
		priv_setop_assert(priv_addset(pset, "file_write"));
		break;
	default:
		REPORT(LOG_ERR, "INCOMPLETE AT: %s(%d)\n", __func__, __LINE__);
		exit(1);
	}
}

/*
 * permitted is initialized from effective (see mgt_sandbox_solaris_waive)
 * so only additionally required privileges need to be added here
 */

static void
mgt_sandbox_solaris_add_permitted(priv_set_t *pset, enum sandbox_e who)
{
	switch (who) {
	case SANDBOX_VCC:
	case SANDBOX_CC:
	case SANDBOX_VCLLOAD:
		break;
	case SANDBOX_WORKER:
		/* for raising limits in cache_waiter_ports.c */
		AZ(priv_addset(pset, PRIV_SYS_RESOURCE));
		break;
	default:
		REPORT(LOG_ERR, "INCOMPLETE AT: %s(%d)\n", __func__, __LINE__);
		exit(1);
	}
}

/*
 * additional privileges needed by mgt_sandbox_solaris_privsep -
 * will get waived in mgt_sandbox_solaris_waive
 */
static void
mgt_sandbox_solaris_add_initial(priv_set_t *pset, enum sandbox_e who)
{
	(void)who;

	/* for setgid/setuid */
	AZ(priv_addset(pset, PRIV_PROC_SETID));
}

/*
 * if we are not yet privilege-aware already (ie we have been started
 * not-privilege aware with euid 0), we try to grab any privileges we
 * will need later.
 * We will reduce to least privileges in mgt_sandbox_solaris_waive
 *
 * We need to become privilege-aware to avoid setuid resetting them.
 */

static void
mgt_sandbox_solaris_init(enum sandbox_e who)
{
	priv_set_t *priv_all;

	if (! (priv_all = priv_allocset())) {
		REPORT(LOG_ERR,
		    "Sandbox warning: "
		    " mgt_sandbox_init - priv_allocset failed: errno=%d (%s)",
		    errno, strerror(errno));
		return;
	}

	priv_emptyset(priv_all);

	mgt_sandbox_solaris_add_inheritable(priv_all, who);
	mgt_sandbox_solaris_add_effective(priv_all, who);
	mgt_sandbox_solaris_add_permitted(priv_all, who);
	mgt_sandbox_solaris_add_initial(priv_all, who);

	/* try to get all possible privileges, expect EPERM here */
	setppriv_assert(setppriv(PRIV_ON, PRIV_PERMITTED, priv_all));
	setppriv_assert(setppriv(PRIV_ON, PRIV_EFFECTIVE, priv_all));
	setppriv_assert(setppriv(PRIV_ON, PRIV_INHERITABLE, priv_all));

	priv_freeset(priv_all);
}

static void
mgt_sandbox_solaris_privsep(enum sandbox_e who)
{
	(void)who;

	if (priv_ineffect(PRIV_PROC_SETID)) {
                if (getgid() != mgt_param.gid)
                        XXXAZ(setgid(mgt_param.gid));
                if (getuid() != mgt_param.uid)
                        XXXAZ(setuid(mgt_param.uid));
        } else {
                REPORT(LOG_INFO,
		    "Privilege %s missing, will not change uid/gid",
		    PRIV_PROC_SETID);
        }
}

/*
 * Waive most privileges in the child
 *
 * as of onnv_151a, we should end up with:
 *
 * > ppriv -v #pid of varnish child
 * PID:  .../varnishd ...
 * flags = PRIV_AWARE
 *      E: file_read,file_write,net_access
 *      I: none
 *      P: file_read,file_write,net_access,sys_resource
 *      L: file_read,file_write,net_access,sys_resource
 *
 * We should keep sys_resource in P in order to adjust our limits if we need to
 */

static void
mgt_sandbox_solaris_waive(enum sandbox_e who)
{
	priv_set_t *effective, *inheritable, *permitted;

	if (!(effective = priv_allocset()) ||
	    !(inheritable = priv_allocset()) ||
	    !(permitted = priv_allocset())) {
		REPORT(LOG_ERR,
		    "Sandbox warning: "
		    " mgt_sandbox_waive - priv_allocset failed: errno=%d (%s)",
		    errno, strerror(errno));
		return;
	}

	/*
	 * simple scheme:
	 *     (inheritable subset-of effective) subset-of permitted
	 */

	priv_emptyset(inheritable);
	mgt_sandbox_solaris_add_inheritable(inheritable, who);

	priv_copyset(inheritable, effective);
	mgt_sandbox_solaris_add_effective(effective, who);

	priv_copyset(effective, permitted);
	mgt_sandbox_solaris_add_permitted(permitted, who);

	/*
	 * invert the sets and clear privileges such that setppriv will always
	 * succeed
	 */
	priv_inverse(inheritable);
	priv_inverse(effective);
	priv_inverse(permitted);

	AZ(setppriv(PRIV_OFF, PRIV_LIMIT, permitted));
	AZ(setppriv(PRIV_OFF, PRIV_PERMITTED, permitted));
	AZ(setppriv(PRIV_OFF, PRIV_EFFECTIVE, effective));
	AZ(setppriv(PRIV_OFF, PRIV_INHERITABLE, inheritable));

	priv_freeset(inheritable);
	priv_freeset(effective);
	priv_freeset(permitted);
}

void __match_proto__(mgt_sandbox_f)
mgt_sandbox_solaris(enum sandbox_e who)
{
	mgt_sandbox_solaris_init(who);
	mgt_sandbox_solaris_privsep(who);
	mgt_sandbox_solaris_waive(who);
}
#endif /* HAVE_SETPPRIV */
