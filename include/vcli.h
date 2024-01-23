/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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
 * Public definition of the CLI protocol, part of the published Varnish-API.
 *
 * The overall structure of the protocol is a command-line like
 * "command+arguments" request and a IETF style "number + string" response.
 *
 * Arguments can contain arbitrary sequences of bytes which are encoded
 * in back-slash notation in double-quoted, if necessary.
 */

/*
 * Status/return codes in the CLI protocol
 */

enum VCLI_status_e {
	CLIS_SYNTAX	= 100,
	CLIS_UNKNOWN	= 101,
	CLIS_UNIMPL	= 102,
	CLIS_TOOFEW	= 104,
	CLIS_TOOMANY	= 105,
	CLIS_PARAM	= 106,
	CLIS_AUTH	= 107,
	CLIS_OK		= 200,
	CLIS_TRUNCATED	= 201,
	CLIS_CANT	= 300,
	CLIS_COMMS	= 400,
	CLIS_CLOSE	= 500
};

typedef enum {
	PROTO_FULL = 0,
	PROTO_STATUS,
	PROTO_HEADLESS
} vcls_proto_e;

/* Length of first line of response */
#define CLI_LINE0_LEN	13
#define CLI_AUTH_RESPONSE_LEN		64	/* 64 hex + NUL */

#if !defined(VCLI_PROTOCOL_ONLY)
/* Convenience functions exported in libvarnishapi */
int VCLI_Write(int fd, vcls_proto_e proto, unsigned status, const char *result);
int VCLI_WriteResult(int fd, unsigned status, const char *result);
int VCLI_ReadResult(int fd, unsigned *status, char **ptr, double tmo);
void VCLI_AuthResponse(int S_fd, const char *challenge,
    char response[CLI_AUTH_RESPONSE_LEN + 1]);
#endif
