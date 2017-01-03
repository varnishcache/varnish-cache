/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Varnish Software AS
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
 * Argument list:
 * ---------------------------------------
 * a	Http header name
 * b	enum name
 * c	Supress header in filter ops
 *
 * see [RFC2616 13.5.1 End-to-end and Hop-by-hop Headers]
 *
 */

/*lint -save -e525 -e539 */

/* Shorthand for this file only, to keep table narrow */

#if defined(P) || defined(F) || defined(I) || defined(H) || defined(S)
#error "Macro overloading"  // Trust but verify
#endif

#define P HTTPH_R_PASS
#define F HTTPH_R_FETCH
#define I HTTPH_A_INS
#define S HTTPH_A_PASS
#define H(s,e,f) HTTPH(s, e, f)

H("Keep-Alive",		H_Keep_Alive,		P|F  |S)	// 2068
H("Accept",		H_Accept,		0      )	// 2616 14.1
H("Accept-Charset",	H_Accept_Charset,	0      )	// 2616 14.2
H("Accept-Encoding",	H_Accept_Encoding,	0      )	// 2616 14.3
H("Accept-Language",	H_Accept_Language,	0      )	// 2616 14.4
H("Accept-Ranges",	H_Accept_Ranges,	P|F|I  )	// 2616 14.5
H("Age",		H_Age,			    I|S)	// 2616 14.6
H("Allow",		H_Allow,		0      )	// 2616 14.7
H("Authorization",	H_Authorization,	0      )	// 2616 14.8
H("Cache-Control",	H_Cache_Control,	  F    )	// 2616 14.9
H("Connection",		H_Connection,		P|F|I|S)	// 2616 14.10
H("Content-Encoding",	H_Content_Encoding,	0      )	// 2616 14.11
H("Content-Language",	H_Content_Language,	0      )	// 2616 14.12
H("Content-Length",	H_Content_Length,	0      )	// 2616 14.13
H("Content-Location",	H_Content_Location,	0      )	// 2616 14.14
H("Content-MD5",	H_Content_MD5,		0      )	// 2616 14.15
H("Content-Range",	H_Content_Range,	  F|I  )	// 2616 14.16
H("Content-Type",	H_Content_Type,		0      )	// 2616 14.17
H("Cookie",		H_Cookie,		0      )	// 6265 4.2
H("Date",		H_Date,			0      )	// 2616 14.18
H("ETag",		H_ETag,			0      )	// 2616 14.19
H("Expect",		H_Expect,		0      )	// 2616 14.20
H("Expires",		H_Expires,		0      )	// 2616 14.21
H("From",		H_From,			0      )	// 2616 14.22
H("Host",		H_Host,			0      )	// 2616 14.23
H("HTTP2-Settings",	H_HTTP2_Settings,	P|F|I|S)	// 7540 3.2.1
H("If-Match",		H_If_Match,		  F    )	// 2616 14.24
H("If-Modified-Since",	H_If_Modified_Since,	  F    )	// 2616 14.25
H("If-None-Match",	H_If_None_Match,	  F    )	// 2616 14.26
H("If-Range",		H_If_Range,		  F    )	// 2616 14.27
H("If-Unmodified-Since",H_If_Unmodified_Since,	  F    )	// 2616 14.28
H("Last-Modified",	H_Last_Modified,	0      )	// 2616 14.29
H("Location",		H_Location,		0      )	// 2616 14.30
H("Max-Forwards",	H_Max_Forwards,		0      )	// 2616 14.31
H("Pragma",		H_Pragma,		0      )	// 2616 14.32
H("Proxy-Authenticate",	H_Proxy_Authenticate,	  F|I  )	// 2616 14.33
H("Proxy-Authorization",H_Proxy_Authorization,	  F|I  )	// 2616 14.34
H("Range",		H_Range,		  F|I  )	// 2616 14.35
H("Referer",		H_Referer,		0      )	// 2616 14.36
H("Retry-After",	H_Retry_After,		0      )	// 2616 14.37
H("Server",		H_Server,		0      )	// 2616 14.38
H("Set-Cookie",		H_Set_Cookie,		0      )	// 6265 4.1
H("TE",			H_TE,			P|F|I|S)	// 2616 14.39
H("Trailer",		H_Trailer,		P|F|I|S)	// 2616 14.40
H("Transfer-Encoding",	H_Transfer_Encoding,	P|F|I|S)	// 2616 14.41
H("Upgrade",		H_Upgrade,		P|F|I|S)	// 2616 14.42
H("User-Agent",		H_User_Agent,		0      )	// 2616 14.43
H("Vary",		H_Vary,			0      )	// 2616 14.44
H("Via",		H_Via,			0      )	// 2616 14.45
H("Warning",		H_Warning,		0      )	// 2616 14.46
H("WWW-Authenticate",	H_WWW_Authenticate,	0      )	// 2616 14.47
H("X-Forwarded-For",	H_X_Forwarded_For,	0      )	// No RFC

#undef P
#undef F
#undef I
#undef S
#undef H
#undef HTTPH

/*lint -restore */
