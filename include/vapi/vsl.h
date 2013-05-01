/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2013 Varnish Software AS
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
struct VSLQ;

struct VSLC_ptr {
	const uint32_t		*ptr; /* Record pointer */
	unsigned		priv;
};

struct VSL_cursor {
	/* The record this cursor points to */
	struct VSLC_ptr		rec;

	/* If not -1, the vxid of all records in this set */
	int32_t			vxid;

	/* For set cursors, the depth level of these records */
	unsigned level;

	/* Nonzero if pointer values from this cursor are still valid
	   after next call to VSL_Next */
	unsigned shmptr_ok;
};

enum VSL_grouping_e {
	VSL_g_raw,
	VSL_g_vxid,
	VSL_g_request,
	VSL_g_session,
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

int VSL_ResetCursor(struct VSL_cursor *c);
	/*
	 * Reset the cursor position to the head, so that the next call to
	 * VSL_Next returns the first record. For VSM cursor, it will
	 * point close to the head of the log, but at least 2 segments away
	 * from the tail.
	 *
	 * Return values:
	 *    -1: Operation not supported
	 */

int VSL_Check(const struct VSL_cursor *c, const struct VSLC_ptr *ptr);
	/*
	 * Check if the VSLC_ptr structure points to a value that is still
	 * valid:
	 *
	 * Return values:
	 *    -1: Operation not supported
	 *     0: Not valid
	 *     1: Valid - warning level
	 *     2: Valid
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

int VSL_PrintVXID(struct VSL_data *vsl, const struct VSL_cursor *c, void *fo);
	/*
	 * Print the log record pointed to by cursor to stream.
	 *
	 * Format: <vxid> <tag> <type> <data>
	 *
	 * Arguments:
	 *   vsl: The VSL_data context
	 *     c: A VSL_cursor
	 *    fo: A FILE* pointer
	 *
	 * Return values:
	 *	0:	OK
	 *     -5:	I/O write error - see errno
	 */

int VSL_PrintLevel(struct VSL_data *vsl, const struct VSL_cursor *c, void *fo);
	/*
	 * Print the log record pointed to by cursor to stream.
	 *
	 * Format: <level> <tag> <type> <data>
	 *
	 * Arguments:
	 *   vsl: The VSL_data context
	 *     c: A VSL_cursor
	 *    fo: A FILE* pointer
	 *
	 * Return values:
	 *	0:	OK
	 *     -5:	I/O write error - see errno
	 */

int VSL_PrintAll(struct VSL_data *vsl, struct VSL_cursor *c, void *fo);
	/*
	 * Calls VSL_Next on c until c is exhausted. In turn calls
	 * prints all records where VSL_Match returns true.
	 *
	 * If c->vxid == -1, calls VSL_PrintVXID on each record. Else
	 * prints a VXID header and calls VSL_PrintLevel on each record.
	 *
	 * Arguments:
	 *   vsl: The VSL_data context
	 *     c: A VSL_cursor
	 *    fo: A FILE* pointer, stdout if NULL
	 *
	 * Return values:
	 *	0:	OK
	 *    !=0:	Return value from either VSL_Next or VSL_Print
	 */

int VSL_PrintSet(struct VSL_data *vsl, struct VSL_cursor *cp[], void *fo);
	/*
	 * Calls VSL_PrintAll on each cursor in cp[]. If any cursor in cp
	 * has vxid != -1 it will end with a double line break as a set
	 * delimeter.
	 *
	 * Arguments:
	 *   vsl: The VSL_data context
	 *    cp: A NULL-terminated array of VSL_cursor pointers
	 *    fo: A FILE* pointer, stdout if NULL
	 *
	 * Return values:
	 *	0:	OK
	 *    !=0:	Return value from either VSL_Next or VSL_PrintAll
	 */

struct VSLQ *VSLQ_New(struct VSL_data *vsl, struct VSL_cursor **cp,
    enum VSL_grouping_e grouping, const char *query);
	/*
	 * Create a new query context using cp. On success cp is NULLed,
	 * and will be deleted when deleting the query.
	 *
	 * Arguments:
	 *       vsl: The VSL_data context
	 *        cp: The cursor to use
	 *  grouping: VXID grouping to report on
	 *     query: Query match expression
	 *
	 * Return values:
	 *  non-NULL: OK
	 *      NULL: Error - see VSL_Error
	 */

void VSLQ_Delete(struct VSLQ **pvslq);
	/*
	 * Delete the query pointed to by pvslq, freeing up the resources
	 */

typedef int VSLQ_dispatch_f(struct VSL_data *vsl, struct VSL_cursor *cp[],
    void *priv);
	/*
	 * The callback function type for use with VSLQ_Dispatch.
	 *
	 * Arguments:
	 *   vsl: The VSL_data context
	 *  cp[]: A NULL terminated array of pointer to cursors. Each cursor
	 *        will iterate over the log records of a single VXID
	 *  priv: The priv argument from VSL_Dispatch
	 *
	 * Return value:
	 *     0: OK - continue
	 *   !=0: Makes VSLQ_Dispatch return with this return value immediatly
	 */

int VSLQ_Dispatch(struct VSLQ *vslq, VSLQ_dispatch_f *func, void *priv);
	/*
	 * Process log and call func for each set matching the specified
	 * query
	 *
	 * Arguments:
	 *  vslq: The VSLQ query
	 *  func: The callback function to call. Can be NULL to ignore records.
	 *  priv: An argument passed to func
	 *
	 * Return values:
	 *     0: No more log records available
	 *   !=0: The return value from either VSL_Next() or func
	 */

int VSLQ_Flush(struct VSLQ *vslq, VSLQ_dispatch_f *func, void *priv);
	/*
	 * Flush any pending record sets from the query.
	 *
	 * Arguments:
	 *  vslq: The VSL context
	 *  func: The callback function to call. Pass NULL to discard the
	 *        pending messages
	 *  priv: An argument passed to func
	 *
	 * Return values:
	 *     0: OK
	 *   !=0: The return value from func
	 */

#endif /* VAPI_VSL_H_INCLUDED */
