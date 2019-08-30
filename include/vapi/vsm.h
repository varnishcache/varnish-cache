/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 *
 * This is the public API for the VSM access.
 *
 * The VSM "class" acts as parent class for the VSL and VSC subclasses.
 *
 */

#ifndef VAPI_VSM_H_INCLUDED
#define VAPI_VSM_H_INCLUDED

struct vsm;

/*
 * This structure is used to reference a VSM chunk
 */

struct vsm_fantom {
	uintptr_t		priv;		/* VSM private */
	uintptr_t		priv2;		/* VSM private */
	void			*b;		/* first byte of payload */
	void			*e;		/* first byte past payload */
	char			*class;
	char			*ident;
};

#define VSM_FANTOM_NULL { 0, 0, 0, 0, 0, 0 }

/*---------------------------------------------------------------------
 * VSM level access functions
 */

struct vsm *VSM_New(void);
	/*
	 * Allocate and initialize a VSL_data handle structure.
	 * This is the first thing you will always have to do.
	 * You can have multiple active vsm handles at the same time
	 * referencing the same or different shared memory files.
	 * Returns:
	 *	Pointer to usable VSL_data handle.
	 *	NULL: malloc failed.
	 */

void VSM_Destroy(struct vsm **vd);
	/*
	 * Close and deallocate all storage and mappings.
	 * (including any VSC and VSL "sub-classes" XXX?)
	 */

const char *VSM_Error(const struct vsm *vd);
	/*
	 * Return the first recorded error message.
	 */

void VSM_ResetError(struct vsm *vd);
	/*
	 * Reset recorded error message.
	 */

#define VSM_n_USAGE	"[-n varnish_name]"
#define VSM_t_USAGE	"[-t <seconds|off>]"

int VSM_Arg(struct vsm *, char flag, const char *arg);
	/*
	 * Handle all VSM specific command line arguments.
	 *
	 * Returns:
	 *	 1 on success
	 *	 <0 on failure, VSM_Error() returns diagnostic string
	 *
	 * 't' Configure patience during startup
	 *
	 *	If arg is "off", VSM_Attach() will wait forever.
	 *	Otherwise arg is the number of seconds to be patient
	 *	while the varnishd manager process gets started.
	 *
	 *	The default is five seconds.
	 *
	 * 'n' Configure varnishd instance to access
	 *
	 *	The default is the hostname.
	 */

int VSM_Attach(struct vsm *, int progress);
	/*
	 * Attach to the master process VSM segment, according to
	 * the 't' argument.  If `progress_fd` is non-negative, a
	 * period ('.') will be output for each second waited, and if
	 * any periods were output, a NL ('\n') is output before the
	 * function returns.
	 *
	 * Returns:
	 *	0	Attached
	 *	-1	Not Attached.
	 */

#define VSM_MGT_RUNNING		(1U<<1)
#define VSM_MGT_CHANGED		(1U<<2)
#define VSM_MGT_RESTARTED	(1U<<3)
#define VSM_WRK_RUNNING		(1U<<9)
#define VSM_WRK_CHANGED		(1U<<10)
#define VSM_WRK_RESTARTED	(1U<<11)

unsigned VSM_Status(struct vsm *);
	/*
	 * Returns a bitmap of the current status and changes in it
	 * since the previous call to VSM_Status
	 */

void VSM__iter0(const struct vsm *, struct vsm_fantom *vf);
int VSM__itern(struct vsm *, struct vsm_fantom *vf);

#define VSM_FOREACH(vf, vd) \
    for (VSM__iter0((vd), (vf)); VSM__itern((vd), (vf));)
	/*
	 * Iterate over all chunks in shared memory
	 * vf = "struct vsm_fantom *"
	 * vd = "struct vsm *"
	 */

int VSM_Map(struct vsm *, struct vsm_fantom *vf);
int VSM_Unmap(struct vsm *, struct vsm_fantom *vf);

struct vsm_valid {
	const char *name;
};

extern const struct vsm_valid VSM_invalid[];
extern const struct vsm_valid VSM_valid[];

const struct vsm_valid *VSM_StillValid(const struct vsm *, const struct vsm_fantom *vf);
	/*
	 * Check the validity of a previously looked up vsm_fantom.
	 *
	 * VSM_invalid means that the SHM chunk this fantom points to does
	 * not exist in the log file any longer.
	 *
	 * VSM_valid means that the SHM chunk this fantom points to is still
	 * good.
	 *
	 * Return:
	 *   VSM_invalid: fantom is not valid any more.
	 *   VSM_valid:   fantom is still the same.
	 */

int VSM_Get(struct vsm *, struct vsm_fantom *vf,
    const char *class, const char *ident);
	/*
	 * Find a chunk, produce fantom for it.
	 * Returns zero on failure.
	 * class is mandatory, ident optional.
	 */

char *VSM_Dup(struct vsm*, const char *class, const char *ident);
	/*
	 * Returns a malloc'ed copy of the fanton.
	 *
	 * Return:
	 *   NULL = Failure
	 *   !NULL = malloc'ed pointer
	 */

#endif /* VAPI_VSM_H_INCLUDED */
