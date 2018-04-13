/*-
 * Copyright 2018 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Authors: Geoffrey Simmons <geoffrey.simmons@uplex.de>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

#if defined(HAVE_GETPEEREID)
#include <unistd.h>
#endif

#if defined(HAVE_GETPEERUCRED)
#include <ucred.h>
# if defined(HAVE_SETPPRIV)
# include <priv.h>
static priv_set_t *priv_proc_info = NULL;
# endif
#endif

#define CREDS_FAIL -1
#define NOT_SUPPORTED -2

#if defined(HAVE_GETPEERUCRED) && defined(HAVE_SETPPRIV)
static void __attribute__((constructor))
cred_compat_init(void)
{
	AZ(priv_proc_info);
	priv_proc_info = priv_allocset();
	AN(priv_proc_info);
	AZ(priv_addset(priv_proc_info, PRIV_PROC_INFO));
}

static void __attribute__((destructor))
cred_compat_fini(void)
{
	if (priv_proc_info == NULL)
		return;
	priv_freeset(priv_proc_info);
	priv_proc_info = NULL;
}
#endif

static int
get_ids(int fd, uid_t *uid, gid_t *gid)
{

#if defined(SO_PEERCRED)

	struct ucred ucred;
	socklen_t l = sizeof(ucred);

	errno = 0;
	if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, (void *) &ucred, &l) != 0)
		return (CREDS_FAIL);
	*uid = ucred.uid;
	*gid = ucred.gid;
	return (0);

#elif defined(HAVE_GETPEEREID)

	errno = 0;
	if (getpeereid(fd, uid, gid) != 0)
		return (CREDS_FAIL);
	return (0);

#elif defined(HAVE_GETPEERUCRED)
	char buf[ucred_size()];
	ucred_t *ucredp = (ucred_t *)buf;

# if defined(HAVE_SETPPRIV)
	priv_set_t *priv = NULL;

	errno = 0;
	if (!priv_ineffect(PRIV_PROC_INFO)) {
		priv = priv_proc_info;
		if (setppriv(PRIV_ON, PRIV_EFFECTIVE, priv))
			return (CREDS_FAIL);
	}
# endif

	errno = 0;
	if (getpeerucred(fd, &ucredp))
		return (CREDS_FAIL);
	*uid = ucred_geteuid(ucredp);
	*gid = ucred_getegid(ucredp);

# if defined(HAVE_SETPPRIV)
	if (priv != NULL)
		AZ(setppriv(PRIV_OFF, PRIV_EFFECTIVE, priv)); // waive
# endif

	return (0);
#else
	(void) fd;
	(void) uid;
	(void) gid;
	return (NOT_SUPPORTED);
#endif

}
