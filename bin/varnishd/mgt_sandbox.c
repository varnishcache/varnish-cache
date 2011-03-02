/*-
 * Copyright (c) 2006-2011 Linpro AS
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
 * Sandboxing child processes
 *
 * The worker/manager process border is one of the major security barriers
 * in Varnish, and therefore subject to whatever restrictions we have access
 * to under the given operating system.
 *
 * Unfortunately there is no consensus on APIs for this purpose, so each
 * operating system will require its own methods.
 *
 * This sourcefile tries to encapsulate the resulting mess on place.
 *
 * TODO:
 *	Unix:	chroot
 *	FreeBSD: jail
 *	FreeBSD: capsicum
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <stdio.h>
#include <syslog.h>
#include <unistd.h>

#ifdef HAVE_PRIV_H
#include <priv.h>
#endif

#ifndef HAVE_SETPROCTITLE
#include "compat/setproctitle.h"
#endif

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include "mgt.h"
#include "heritage.h"

/*--------------------------------------------------------------------*/

/* Waive all privileges in the child, it does not need any */

void
mgt_sandbox(void)
{

	if (geteuid() == 0) {
		XXXAZ(setgid(params->gid));
		XXXAZ(setuid(params->uid));
	} else {
		REPORT0(LOG_INFO, "Not running as root, no priv-sep");
	}

	/* On Linux >= 2.4, you need to set the dumpable flag
	   to get core dumps after you have done a setuid. */

#ifdef __linux__
	if (prctl(PR_SET_DUMPABLE, 1) != 0)
		REPORT0(LOG_INFO,
		    "Could not set dumpable bit.  Core dumps turned off\n");
#endif

#ifdef HAVE_SETPPRIV
	priv_set_t *empty, *minimal;

	if (!(empty = priv_allocset()) ||
	    !(minimal = priv_allocset())) {
		REPORT0(LOG_ERR, "priv_allocset_failed");
	} else {
		priv_emptyset(empty);
		priv_emptyset(minimal);

		/*
		 * new privilege,
		 * silently ignore any errors if it doesn't exist
		 */
		priv_addset(minimal, "net_access");

#define SETPPRIV(which, set)				       		\
		if (setppriv(PRIV_SET, which, set))			\
			REPORT0(LOG_ERR,				\
			    "Waiving privileges failed on " #which)

		/* need to set I after P to avoid SNOCD being set */
		SETPPRIV(PRIV_LIMIT, minimal);
		SETPPRIV(PRIV_PERMITTED, minimal); /* implies PRIV_EFFECTIVE */
		SETPPRIV(PRIV_INHERITABLE, empty);

		priv_freeset(empty);
		priv_freeset(minimal);
	}
#endif

}
