/*-
 * Copyright (c) 2006-2011 Varnish Software AS
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

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include <stdio.h>
#include <syslog.h>
#include <unistd.h>

#include "mgt/mgt.h"
#include "common/params.h"

/*--------------------------------------------------------------------*/

#ifndef HAVE_SETPPRIV
static void __match_proto__(mgt_sandbox_f)
mgt_sandbox_unix(enum sandbox_e who)
{
	(void)who;
	if (geteuid() == 0) {
		XXXAZ(setgid(mgt_param.gid));
		XXXAZ(setuid(mgt_param.uid));
	} else {
		REPORT0(LOG_INFO, "Not running as root, no priv-sep");
	}
}
#endif

/*--------------------------------------------------------------------*/

#ifdef __linux__
static void __match_proto__(mgt_sandbox_f)
mgt_sandbox_linux(enum sandbox_e who)
{
	mgt_sandbox_unix(who);

	if (prctl(PR_SET_DUMPABLE, 1) != 0) {
		REPORT0(LOG_INFO,
		    "Could not set dumpable bit.  Core dumps turned off\n");
	}
}
#endif


/*--------------------------------------------------------------------*/

mgt_sandbox_f *mgt_sandbox =
#ifdef HAVE_SETPPRIV
	mgt_sandbox_solaris;
#elif defined (__linux__)
	mgt_sandbox_linux;
#else
	mgt_sandbox_unix;
#endif
