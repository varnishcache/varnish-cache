/*-
 * Copyright (c) 2006 Verdens Gang AS
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
 * This is the public API for the VSL access.
 *
 */

#ifndef VAPI_VSL_H_INCLUDED
#define VAPI_VSL_H_INCLUDED

#include <stdio.h>

#include "vapi/vsm.h"
#include "vapi/vsl_int.h"

#define VSL_ARGS	"i:x:"

#define VSL_i_USAGE	"[-i tag]"
#define VSL_x_USAGE	"[-x tag]"

#define VSL_USAGE	"[...] "		\
			VSL_i_USAGE " "		\
			VSL_x_USAGE

struct VSL_data;

struct VSL_cursor {
	const uint32_t		*ptr; /* Record pointer */
};

extern const char *VSL_tags[256];
	/*
	 * Tag to string array.  Contains NULL for invalid tags.
	 */

int VSL_Name2Tag(const char *name, int l);
	/*
	 * Convert string to tag number (= enum VSL_tag_e)
	 *
	 * Return values:
	 *	>=0:	Tag number
	 *	-1:	No tag matches
	 *	-2:	Multiple tags match substring
	 */

struct VSL_data *VSL_New(void);
int VSL_Arg(struct VSL_data *vsl, int opt, const char *arg);
	/*
	 * Handle standard log-presenter arguments
	 * Return:
	 *	-1 error, VSL_Error() returns diagnostic string
	 *	 0 not handled
	 *	 1 Handled.
	 */

void VSL_Delete(struct VSL_data *vsl);
	/*
	 * Delete a VSL context, freeing up the resources
	 */

const char *VSL_Error(const struct VSL_data *vsl);
	/*
	 * Return the latest error message.
	 */

void VSL_ResetError(struct VSL_data *vsl);
	/*
	 * Reset any error message.
	 */

struct VSL_cursor *VSL_CursorVSM(struct VSL_data *vsl, struct VSM_data *vsm,
    int tail);
       /*
        * Set the cursor pointed to by cursor up as a raw cursor in the
        * log. If tail is non-zero, it will point to the tail of the
        * log. Is tail is zero, it will point close to the head of the
        * log, at least 2 segments away from the head.
	*
	* Return values:
	* non-NULL: Pointer to cursor
	*     NULL: Error, see VSL_Error
        */

struct VSL_cursor *VSL_CursorFile(struct VSL_data *vsl, const char *name);
	/*
	 * Create a cursor pointing to the beginning of the binary VSL log
	 * in file name. If name is '-' reads from stdin.
	 *
	 * Return values:
	 * non-NULL: Pointer to cursor
	 *     NULL: Error, see VSL_Error
	 */

void VSL_DeleteCursor(struct VSL_cursor *c);
	/*
	 * Delete the cursor pointed to by c
	 */

int VSL_Next(struct VSL_cursor *c);
	/*
	 * Return raw pointer to next VSL record.
	 *
	 * Return values:
	 *	1:	Cursor points to next log record
	 *	0:	End of log
	 *     -1:	End of file (-r) (XXX / -k arg exhausted / "done")
	 *     -2:	Remote abandoned or closed
	 *     -3:	Overrun
	 *     -4:	I/O read error - see errno
	 */

int VSL_Match(struct VSL_data *vsl, const struct VSL_cursor *c);
	/*
	 * Returns true if the record pointed to by cursor matches the
	 * record current record selectors
	 *
	 * Return value:
	 *	1:	Match
	 *	0:	No match
	 */

int VSL_Print(struct VSL_data *vsl, const struct VSL_cursor *c, void *file);
	/*
	 * Print the log record pointed to by cursor to stream.
	 *
	 * Return values:
	 *	0:	OK
	 *     -5:	I/O write error - see errno
	 */

#endif /* VAPI_VSL_H_INCLUDED */
