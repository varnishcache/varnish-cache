/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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
 * $Id$
 *
 * Argument list:
 * ---------------------------------------
 * a	Http header name
 * b	session field name
 * c	Request(1)/Response(2) bitfield
 * d	(obsolete)
 * e	Supress header in filter ops
 * f	unused
 * g	unused
 *
 * see [RFC2616 13.5.1 End-to-end and Hop-by-hop Headers]
 *
 */

#ifndef HTTPH_R_PASS
#define HTTPH_R_PASS	(1 << 0)	/* Request (c->b) in pass mode */
#define HTTPH_A_PASS	(1 << 1)	/* Response (b->c)in pass mode */
#define HTTPH_R_PIPE	(1 << 2)	/* Request (c->b) in pipe mode */
#define HTTPH_R_FETCH	(1 << 3)	/* Request (c->b) for fetch */
#define HTTPH_A_INS	(1 << 4)	/* Response (b->o) for insert */
#define HTTPH_A_DELIVER	(1 << 5)	/* Response (o->c) for deliver */
#endif

HTTPH("Keep-Alive",		H_Keep_Alive,		3, 3, HTTPH_R_PASS | HTTPH_A_PASS | HTTPH_R_FETCH,					0, 0)	/* RFC2068 */
HTTPH("Accept",			H_Accept,		1, 0, 0,										0, 0)	/* RFC2616 14.1 */
HTTPH("Accept-Charset",		H_Accept_Charset,	1, 0, 0,										0, 0)	/* RFC2616 14.2 */
HTTPH("Accept-Encoding",	H_Accept_Encoding,	1, 0, 0,										0, 0)	/* RFC2616 14.3 */
HTTPH("Accept-Language",	H_Accept_Language,	1, 0, 0,										0, 0)	/* RFC2616 14.4 */
HTTPH("Accept-Ranges",		H_Accept_Ranges,	2, 3, HTTPH_R_PASS | HTTPH_A_PASS | HTTPH_R_FETCH | HTTPH_A_INS,			0, 0)	/* RFC2616 14.5 */
HTTPH("Age",			H_Age,			2, 0, 						    HTTPH_A_INS,			0, 0)	/* RFC2616 14.6 */
HTTPH("Allow",			H_Allow,		2, 0, 0,										0, 0)	/* RFC2616 14.7 */
HTTPH("Authorization",		H_Authorization,	1, 0, 0,										0, 0)	/* RFC2616 14.8 */
HTTPH("Cache-Control",		H_Cache_Control,	3, 3, HTTPH_R_PASS | 		    HTTPH_R_FETCH,					0, 0)	/* RFC2616 14.9 */
HTTPH("Connection",		H_Connection,		3, 3, HTTPH_R_PASS | HTTPH_A_PASS | HTTPH_R_FETCH | HTTPH_A_INS,			0, 0)	/* RFC2616 14.10 */
HTTPH("Content-Encoding",	H_Content_Encoding,	2, 0, 0,										0, 0)	/* RFC2616 14.11 */
HTTPH("Content-Langugae",	H_Content_Language,	2, 0, 0,										0, 0)	/* RFC2616 14.12 */
HTTPH("Content-Length",		H_Content_Length,	2, 2, 				    HTTPH_R_FETCH | HTTPH_A_INS,			0, 0)	/* RFC2616 14.13 */
HTTPH("Content-Location",	H_Content_Location,	2, 0, 0,										0, 0)	/* RFC2616 14.14 */
HTTPH("Content-MD5",		H_Content_MD5,		2, 0, 0,										0, 0)	/* RFC2616 14.15 */
HTTPH("Content-Range",		H_Content_Range,	2, 3, HTTPH_R_PASS | HTTPH_A_PASS | HTTPH_R_FETCH | HTTPH_A_INS,			0, 0)	/* RFC2616 14.16 */
HTTPH("Content-Type",		H_Content_Type,		2, 0, 0,										0, 0)	/* RFC2616 14.17 */
HTTPH("Date",			H_Date,			2, 0, 								HTTPH_A_DELIVER,	0, 0)	/* RFC2616 14.18 */
HTTPH("ETag",			H_ETag,			2, 0, 0,										0, 0)	/* RFC2616 14.19 */
HTTPH("Expect",			H_Expect,		1, 0, 0,										0, 0)	/* RFC2616 14.20 */
HTTPH("Expires",		H_Expires,		2, 0, 0,										0, 0)	/* RFC2616 14.21 */
HTTPH("From",			H_From,			1, 0, 0,										0, 0)	/* RFC2616 14.22 */
HTTPH("Host",			H_Host,			1, 0, 0,										0, 0)	/* RFC2616 14.23 */
HTTPH("If-Match",		H_If_Match,		1, 1, 				    HTTPH_R_FETCH,					0, 0)	/* RFC2616 14.24 */
HTTPH("If-Modified-Since",	H_If_Modified_Since,	1, 1, 				    HTTPH_R_FETCH,					0, 0)	/* RFC2616 14.25 */
HTTPH("If-None-Match",		H_If_None_Match,	1, 1, 				    HTTPH_R_FETCH,					0, 0)	/* RFC2616 14.26 */
HTTPH("If-Range",		H_If_Range,		1, 1, 				    HTTPH_R_FETCH,					0, 0)	/* RFC2616 14.27 */
HTTPH("If-Unmodified-Since",	H_If_Unmodifed_Since,	1, 1, 				    HTTPH_R_FETCH,					0, 0)	/* RFC2616 14.28 */
HTTPH("Last-Modified",		H_Last_Modified,	2, 0, 0,										0, 0)	/* RFC2616 14.29 */
HTTPH("Location",		H_Location,		2, 0, 0,										0, 0)	/* RFC2616 14.30 */
HTTPH("Max-Forwards",		H_Max_Forwards,		1, 0, 0,										0, 0)	/* RFC2616 14.31 */
HTTPH("Pragma",			H_Pragma,		1, 0, 0,										0, 0)	/* RFC2616 14.32 */
HTTPH("Proxy-Authenticate",	H_Proxy_Authenticate,	2, 3, 				    HTTPH_R_FETCH | HTTPH_A_INS,			0, 0)	/* RFC2616 14.33 */
HTTPH("Proxy-Authorization",	H_Proxy_Authorization,	1, 3, 				    HTTPH_R_FETCH | HTTPH_A_INS,			0, 0)	/* RFC2616 14.34 */
HTTPH("Range",			H_Range,		1, 0,                  		    HTTPH_R_FETCH | HTTPH_A_INS,			0, 0)	/* RFC2616 14.35 */
HTTPH("Referer",		H_Referer,		1, 0, 0,										0, 0)	/* RFC2616 14.36 */
HTTPH("Retry-After",		H_Retry_After,		2, 0, 0,										0, 0)	/* RFC2616 14.37 */
HTTPH("Server",			H_Server,		2, 0, 0,										0, 0)	/* RFC2616 14.38 */
HTTPH("TE",			H_TE,			1, 3, HTTPH_R_PASS | HTTPH_A_PASS | HTTPH_R_FETCH | HTTPH_A_INS,			0, 0)	/* RFC2616 14.39 */
HTTPH("Trailer",		H_Trailer,		1, 3, HTTPH_R_PASS | HTTPH_A_PASS | HTTPH_R_FETCH | HTTPH_A_INS,			0, 0)	/* RFC2616 14.40 */
HTTPH("Transfer-Encoding",	H_Transfer_Encoding,	2, 3, HTTPH_R_PASS | HTTPH_A_PASS | HTTPH_R_FETCH | HTTPH_A_INS,			0, 0)	/* RFC2616 14.41 */
HTTPH("Upgrade",		H_Upgrade,		2, 3, HTTPH_R_PASS | HTTPH_A_PASS | HTTPH_R_FETCH | HTTPH_A_INS,			0, 0)	/* RFC2616 14.42 */
HTTPH("User-Agent",		H_User_Agent,		1, 0, 0,										0, 0)	/* RFC2616 14.43 */
HTTPH("Vary",			H_Vary,			2, 0, 0,										0, 0)	/* RFC2616 14.44 */
HTTPH("Via",			H_Via,			2, 0, 0,										0, 0)	/* RFC2616 14.45 */
HTTPH("Warning",		H_Warning,		2, 0, 0,										0, 0)	/* RFC2616 14.46 */
HTTPH("WWW-Authenticate",	H_WWW_Authenticate,	2, 0, 0,										0, 0)	/* RFC2616 14.47 */
