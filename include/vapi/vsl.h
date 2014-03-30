/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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

/*
 * enum VSL_tag_e enumerates the SHM log tags, where the identifiers are
 * "SLT_" + XML tag, as defined in tbl/vsl_tags.h. Use the macro SLT__MAX
 * for the highest possible value of the enum.
 * (VSL_tag_e and SLT__MAX included from vsl_int.h)
 */

struct VSL_data;
struct VSLQ;

struct VSLC_ptr {
	const uint32_t		*ptr; /* Record pointer */
	unsigned		priv;
};
	/*
	 * Use these macros to access fields of a VSLC_ptr record
	 * (included from vsl_int.h):
	 *
	 * VSL_TAG(ptr)
	 *   SLT tag (enum VSL_tag_e)
	 *
	 * VSL_ID(ptr)
	 *   VXID
	 *
	 * VSL_CDATA(ptr)
	 *   Payload (as const char *)
	 *
	 * VSL_LEN(ptr)
	 *   Length of the payload in bytes
	 *
	 * VSL_CLIENT(ptr)
	 *   Non-zero if this is a client transaction
	 *
	 * VSL_BACKEND(ptr)
	 *   Non-zero if this is a backend transaction
	 */

struct VSL_cursor {
	/* The record this cursor points to */
	struct VSLC_ptr		rec;

	/* Private data */
	const void		*priv_tbl;
	void			*priv_data;
};

enum VSL_transaction_e {
	VSL_t_unknown,
	VSL_t_sess,
	VSL_t_req,
	VSL_t_bereq,
	VSL_t_raw,
	VSL_t__MAX,
};

enum VSL_reason_e {
	VSL_r_unknown,
	VSL_r_http_1,
	VSL_r_rxreq,
	VSL_r_esi,
	VSL_r_restart,
	VSL_r_pass,
	VSL_r_fetch,
	VSL_r_bgfetch,
	VSL_r__MAX,
};

struct VSL_transaction {
	unsigned		level;
	int32_t			vxid;
	int32_t			vxid_parent;
	enum VSL_transaction_e	type;
	enum VSL_reason_e	reason;
	struct VSL_cursor	*c;
};

enum VSL_grouping_e {
	VSL_g_raw,
	VSL_g_vxid,
	VSL_g_request,
	VSL_g_session,
	VSL_g__MAX,
};

typedef int VSLQ_dispatch_f(struct VSL_data *vsl,
    struct VSL_transaction * const trans[], void *priv);
	/*
	 * The callback function type for use with VSLQ_Dispatch.
	 *
	 * Arguments:
	 *      vsl: The VSL_data context
	 *  trans[]: A NULL terminated array of pointers to VSL_transaction.
	 *     priv: The priv argument from VSL_Dispatch
	 *
	 * Return value:
	 *     0: OK - continue
	 *   !=0: Makes VSLQ_Dispatch return with this return value immediatly
	 */

typedef void VSL_tagfind_f(int tag, void *priv);
	/*
	 * The callback function type for use with VSL_Glob2Tags and
	 * VSL_List2Tags..
	 *
	 * Arguments:
	 *    tag: Tag number (= enum VSL_tag_e)
	 *   priv: The priv argument
	 */

extern const char * const VSL_tags[SLT__MAX];
	/*
	 * Tag to string array.  Contains NULL for invalid tags.
	 */

extern const unsigned VSL_tagflags[SLT__MAX];
	/*
	 * Tag flags array.
	 * Use these macros with VSL_tagflags (included from vsl_int.h):
	 *
	 * VSL_tagflags[tag] & SLT_F_BINARY
	 *   Non-zero if the payload with this tag may include
	 *   non-printable characters
	 *
	 * VSL_tagflags[tag] & SLT_F_UNUSED
	 *   Non-zero if this tag is reserved for future use
	 */

int VSL_Name2Tag(const char *name, int l);
	/*
	 * Convert string to tag number (= enum VSL_tag_e). Name can be a
	 * substring from the beginning of a tag when that substring is
	 * unique. Matching is case insensitive.
	 *
	 * Arguments:
	 *    name:	A tag name (or substring) to match against
	 *       l:	The length of name, or -1 to use strlen.
	 *
	 * Return values:
	 *	>=0:	Tag number
	 *	-1:	No tag matches
	 *	-2:	Multiple tags match substring
	 */

int VSL_Glob2Tags(const char *glob, int l, VSL_tagfind_f *func, void *priv);
	/*
	 * Convert a string to multiple tag matches. The string can have
	 * either a prefix or postfix wildcard (*) character. For each
	 * matching tag func is called. Matching is done case insensitive.
	 *
	 * Arguments:
	 *   glob: The string to match
	 *      l: The length of glob. -1 to use strlen.
	 *   func: The function to call (can be NULL)
	 *   priv: An argument that will be passed to func.
	 *
	 * Return values:
	 *     >0: Number of times func was called for matching tags.
	 *     -1: No tag matches
	 *     -2: Multiple tags match non-glob input
	 *     -3: Syntax error
	 */

int VSL_List2Tags(const char *list, int l, VSL_tagfind_f *func, void *priv);
	/*
	 * Convert a comma-separated list of tag globs to tag
	 * matches. Calls VSL_Glob2Tags for each comma-separated part of
	 * list.
	 *
	 * Arguments:
	 *   list: The list of globs
	 *      l: The length of list. -1 to use strlen
	 *   func: The function to call (can be NULL)
	 *   priv: An argument that will be passed to func.
	 *
	 * Return valus:
	 *     >0: Number of times func was called for matching tags.
	 *     -1: No tag matches for list element
	 *     -2: Multiple tags match non-glob list element
	 *     -3: Syntax error
	 */

extern const char *VSLQ_grouping[VSL_g__MAX];
	/*
	 * Grouping mode to string array.
	 */

int VSLQ_Name2Grouping(const char *name, int l);
	/*
	 * Convert string to grouping (= enum VSL_grouping_e)
	 *
	 * Return values:
	 *	>=0:	Grouping value
	 *	-1:	No grouping type matches
	 *	-2:	Multiple grouping types match substring
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

#define VSL_COPT_TAIL		(1 << 0)
#define VSL_COPT_BATCH		(1 << 1)
#define VSL_COPT_TAILSTOP	(1 << 2)
struct VSL_cursor *VSL_CursorVSM(struct VSL_data *vsl, struct VSM_data *vsm,
    unsigned options);
       /*
        * Set the cursor pointed to by cursor up as a raw cursor in the
        * log. Cursor points at the current log head.
	*
	* Options:
	*   VSL_COPT_TAIL	Start cursor at log tail
	*   VSL_COPT_BATCH	Return batch records
	*   VSL_COPT_TAILSTOP	Return EOF when reaching the log tail
	*
	* Return values:
	* non-NULL: Pointer to cursor
	*     NULL: Error, see VSL_Error
        */

struct VSL_cursor *VSL_CursorFile(struct VSL_data *vsl, const char *name,
    unsigned options);
	/*
	 * Create a cursor pointing to the beginning of the binary VSL log
	 * in file name. If name is '-' reads from stdin.
	 *
	 * Options:
	 *   NONE
	 *
	 * Return values:
	 * non-NULL: Pointer to cursor
	 *     NULL: Error, see VSL_Error
	 */

void VSL_DeleteCursor(const struct VSL_cursor *c);
	/*
	 * Delete the cursor pointed to by c
	 */

int VSL_ResetCursor(const struct VSL_cursor *c);
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

int VSL_Next(const struct VSL_cursor *c);
	/*
	 * Return raw pointer to next VSL record.
	 *
	 * Return values:
	 *	1:	Cursor points to next log record
	 *	0:	End of log
	 *     -1:	End of file
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

int VSL_Print(const struct VSL_data *vsl, const struct VSL_cursor *c, void *fo);
	/*
	 * Print the log record pointed to by cursor to stream.
	 *
	 * Format: (t=type)
	 * 1234567890 12345678901234 1 ...
	 *       vxid tag            t content
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

int VSL_PrintTerse(const struct VSL_data *vsl, const struct VSL_cursor *c,
    void *fo);
	/*
	 * Print the log record pointed to by cursor to stream.
	 *
	 * Format:
	 * 12345678901234 ...
	 * tag            content
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

int VSL_PrintAll(struct VSL_data *vsl, const struct VSL_cursor *c, void *fo);
	/*
	 * Calls VSL_Next on c until c is exhausted. In turn calls
	 * VSL_Print on all records where VSL_Match returns true.
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

VSLQ_dispatch_f VSL_PrintTransactions;
	/*
	 * Prints out each transaction in the array ptrans. For
	 * transactions of level > 0 it will print a header before the log
	 * records. The records will for level == 0 (single records) or if
	 * v_opt is set, be printed by VSL_Print. Else VSL_PrintTerse is
	 * used.
	 *
	 * Arguments:
	 *   vsl: The VSL_data context
	 *    cp: A NULL-terminated array of VSL_cursor pointers
	 *    fo: A FILE* pointer, stdout if NULL
	 *
	 * Return values:
	 *	0:	OK
	 *    !=0:	Return value from either VSL_Next or VSL_Print
	 */

FILE *VSL_WriteOpen(struct VSL_data *vsl, const char *name, int append,
		    int unbuffered);
	/*
	 * Open file name for writing using the VSL_Write* functions. If
	 * append is true, the file will be opened for appending.
	 *
	 * Arguments:
	 *     vsl: The VSL data context
	 *    name: The file name
	 *  append: If true, the file will be appended instead of truncated
	 *   unbuf: If true, use unbuffered mode
	 *
	 * Return values:
	 *     NULL: Error - see VSL_Error
	 * non-NULL: Success
	 */


int VSL_Write(const struct VSL_data *vsl, const struct VSL_cursor *c, void *fo);
	/*
	 * Write the currect record pointed to be c to the FILE* fo
	 *
	 * Return values:
	 *    0: Success
	 *   -5: I/O error - see VSL_Error
	 */

int VSL_WriteAll(struct VSL_data *vsl, const struct VSL_cursor *c, void *fo);
	/*
	 * Calls VSL_Next on c until c is exhausted. In turn calls
	 * VSL_Write on all records where VSL_Match returns true.
	 *
	 * Return values:
	 *	0:	OK
	 *    !=0:	Return value from either VSL_Next or VSL_Write
	 */

VSLQ_dispatch_f VSL_WriteTransactions;
	/*
	 * Write all transactions in ptrans using VSL_WriteAll
	 * Return values:
	 *	0:	OK
	 *    !=0:	Return value from either VSL_Next or VSL_Write
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
	 *     1: Call again
	 *     0: No more log records available
	 *   !=0: The error code from VSL_Next() or func returned non-zero
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
