/*-
 * Copyright (c) 2006-2011 Varnish Software AS
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

#include <stdio.h>
#include <syslog.h>
#include <unistd.h>
#include <string.h>

#ifdef HAVE_PRIV_H
#include <priv.h>
#endif
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "mgt.h"
#include "heritage.h"

/*--------------------------------------------------------------------
 * SOLARIS PRIVILEGES: Note on use of symbolic PRIV_* constants
 * 
 * For privileges which existed in Solaris 10 FCS, we may use the constants from
 * sys/priv_names.h
 *
 * For privileges which have been added later, we need to use strings in order
 * not to break builds of varnish on these platforms. To remain binary
 * compatible, we need to silently ignore errors from priv_addset when using
 * these strings.
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

/* effective during runtime of the child */
static inline void
mgt_sandbox_solaris_add_effective(priv_set_t *pset)
{
	/* PSARC/2009/685 - 8eca52188202 - onnv_132 */
	priv_addset(pset, "net_access");

	/* PSARC/2009/378 - 63678502e95e - onnv_140 */
	priv_addset(pset, "file_read");
	priv_addset(pset, "file_write");
}

/* permitted during runtime of the child - for privilege bracketing */
static inline void
mgt_sandbox_solaris_add_permitted(priv_set_t *pset)
{
	/* for raising limits in cache_waiter_ports.c */
	priv_addset(pset, PRIV_SYS_RESOURCE);
}

/* effective during mgt_sandbox */
static inline void
mgt_sandbox_solaris_add_initial(priv_set_t *pset)
{
	/* for setgid/setuid */
	priv_addset(pset, PRIV_PROC_SETID);
}

/*
 * if we are not yet privilege-aware already (ie we have been started
 * not-privilege aware wird euid 0), we need to grab any additional privileges
 * needed during mgt_standbox, until we reduce to least privileges in
 * mgt_sandbox_waive, otherwise we would loose them with setuid()
 */

void
mgt_sandbox_solaris_init(void)
{
	priv_set_t *priv_all;

	if (! (priv_all = priv_allocset())) {
		REPORT(LOG_ERR,
		    "Child start warning: mgt_sandbox_init - priv_allocset failed: errno=%d (%s)",
		    errno, strerror(errno));
		return;
	}
	
	priv_emptyset(priv_all);

	mgt_sandbox_solaris_add_effective(priv_all);
	mgt_sandbox_solaris_add_permitted(priv_all);
	mgt_sandbox_solaris_add_initial(priv_all);

	setppriv(PRIV_ON, PRIV_PERMITTED, priv_all);
	setppriv(PRIV_ON, PRIV_EFFECTIVE, priv_all);
	setppriv(PRIV_ON, PRIV_INHERITABLE, priv_all);

	priv_freeset(priv_all);
}

void
mgt_sandbox_solaris_privsep(void)
{
	if (priv_ineffect(PRIV_PROC_SETID)) {
                if (getgid() != params->gid)
                        XXXAZ(setgid(params->gid));
                if (getuid() != params->uid)
                        XXXAZ(setuid(params->uid));
        } else {
                REPORT(LOG_INFO, "Privilege %s missing, will not change uid/gid",
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

void
mgt_sandbox_solaris_fini(void)
{
	priv_set_t *effective, *inheritable, *permitted;

	if (!(effective = priv_allocset()) ||
	    !(inheritable = priv_allocset()) ||
	    !(permitted = priv_allocset())) {
		REPORT(LOG_ERR,
		    "Child start warning: mgt_sandbox_waive - priv_allocset failed: errno=%d (%s)",
		    errno, strerror(errno));
		return;
	}

	priv_emptyset(inheritable);

	priv_emptyset(effective);
	mgt_sandbox_solaris_add_effective(effective);

	priv_copyset(effective, permitted);
	mgt_sandbox_solaris_add_permitted(permitted);

	/* 
	 * invert the sets and clear privileges such that setppriv will always
	 * succeed
	 */
	priv_inverse(inheritable);
	priv_inverse(effective);
	priv_inverse(permitted);

#define SETPPRIV(which, set)						\
	if (setppriv(PRIV_OFF, which, set))				\
		REPORT(LOG_ERR,						\
		    "Child start warning: Waiving privileges failed on %s: errno=%d (%s)", \
		    #which, errno, strerror(errno));

	SETPPRIV(PRIV_LIMIT, permitted);
	SETPPRIV(PRIV_PERMITTED, permitted);
	SETPPRIV(PRIV_EFFECTIVE, effective);
	SETPPRIV(PRIV_INHERITABLE, inheritable);
#undef SETPPRIV

	priv_freeset(inheritable);
	priv_freeset(effective);
}

#endif /* HAVE_SETPPRIV */
