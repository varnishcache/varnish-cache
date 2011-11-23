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
 * VSL is a "subclass" of VSM.
 *
 * VSL can either read from VSM or from a file.
 *
 * When reading from a file, the filename is passed in with:
 *	VSL_Arg(vd, "r", "/some/file");
 * and once VSL_Dispatch()/VSL_NextSLT() will indicate EOF by returning -2.
 * Another file can then be opened with VSL_Arg() and processed.
 *
 */

#ifndef VAPI_VSL_H_INCLUDED
#define VAPI_VSL_H_INCLUDED

#include "vapi/vsl_int.h"

struct VSM_data;

/*---------------------------------------------------------------------
 * VSL level access functions
 */

#define VSL_ARGS	"bCcdI:i:k:n:r:s:X:x:m:"
#define VSL_b_USAGE	"[-b]"
#define VSL_c_USAGE	"[-c]"
#define VSL_C_USAGE	"[-C]"
#define VSL_d_USAGE	"[-d]"
#define VSL_i_USAGE	"[-i tag]"
#define VSL_I_USAGE	"[-I regexp]"
#define VSL_k_USAGE	"[-k keep]"
#define VSL_m_USAGE	"[-m tag:regex]"
#define VSL_n_USAGE	VSM_n_USAGE
#define VSL_r_USAGE	"[-r file]"
#define VSL_s_USAGE	"[-s skip]"
#define VSL_x_USAGE	"[-x tag]"
#define VSL_X_USAGE	"[-X regexp]"

#define VSL_USAGE	"[-bCcd] "		\
			VSL_i_USAGE " "		\
			VSL_I_USAGE " "		\
			VSL_k_USAGE " "		\
			VSL_m_USAGE " "		\
			VSL_n_USAGE " "		\
			VSL_r_USAGE " "		\
			VSL_s_USAGE " "		\
			VSL_X_USAGE " "		\
			VSL_x_USAGE

int VSL_Arg(struct VSM_data *vd, int arg, const char *opt);
	/*
	 * Handle standard log-presenter arguments
	 * Return:
	 *	-1 error, VSM_Error() returns diagnostic string
	 *	 0 not handled
	 *	 1 Handled.
	 */

typedef int VSL_handler_f(void *priv, enum VSL_tag_e tag, unsigned fd,
    unsigned len, unsigned spec, const char *ptr, uint64_t bitmap);
	/*
	 * This is the call-back function you must provide.
	 *	priv is whatever you asked for it to be.
	 *	tag is the SLT_mumble tag
	 *	fd is the filedescriptor associated with this record
	 *	len is the length of the data at ptr
	 *	spec are the VSL_S_* flags
	 *	ptr points to the data, beware of non-printables.
	 *	bitmap is XXX ???
	 */

#define VSL_S_CLIENT	(1 << 0)
#define VSL_S_BACKEND	(1 << 1)

VSL_handler_f VSL_H_Print;
	/*
	 * This call-back function will printf() the record to the FILE *
	 * specified in priv.
	 */

void VSL_Select(struct VSM_data *vd, enum VSL_tag_e tag);
	/*
	 * This adds tags which shall always be selected, similar to using
	 * the '-i' option.
	 * VSL_Select()/-i takes precedence over all other filtering.
	 */

int VSL_Dispatch(struct VSM_data *vd, VSL_handler_f *func, void *priv);
	/*
	 * Call func(priv, ...) for all filtered VSL records.
	 *
	 * Return values:
	 *	!=0:	Non-zero return value from func()
	 *	0:	no VSL records.
	 *	-1:	VSL chunk was abandonned.
	 *	-2:	End of file (-r) / -k arg exhausted / "done"
	 */

int VSL_NextSLT(struct VSM_data *lh, uint32_t **pp, uint64_t *bitmap);
	/*
	 * Return raw pointer to next filtered VSL record.
	 *
	 * Return values:
	 *	1:	Valid VSL record at *pp
	 *	0:	no VSL records
	 *	-1:	VSL cunkwas abandonned
	 *	-2:	End of file (-r) / -k arg exhausted / "done"
	 */

int VSL_Matched(struct VSM_data *vd, uint64_t bitmap);
	/*
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

extern const char *VSL_tags[256];
	/*
	 * Tag to string array.  Contains NULL for invalid tags.
	 */

#endif /* VAPI_VSL_H_INCLUDED */
