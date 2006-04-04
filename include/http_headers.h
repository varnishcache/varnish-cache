/*
 * $Id$
 *
 * a	Http header name
 * b	session field name
 * c	PassThrough handling (0=remove, 1=pass)
 * d	unused
 * e	unused
 * f	unused
 * g	unused
 *
 *    a                         b                       c  d  e  f  g 
 *--------------------------------------------------------------------
 */
HTTPH("Accept-Charset",		H_Accept_Charset,	1, 0, 0, 0, 0)
HTTPH("Accept-Encoding",	H_Accept_Encoding,	1, 0, 0, 0, 0)
HTTPH("Accept-Language",	H_Accept_Language,	1, 0, 0, 0, 0)
HTTPH("Accept",			H_Accept,		1, 0, 0, 0, 0)
HTTPH("Authorization",		H_Authorization,	1, 0, 0, 0, 0)
HTTPH("Connection",		H_Connection,		0, 0, 0, 0, 0)
HTTPH("Expect",			H_Expect,		1, 0, 0, 0, 0)
HTTPH("From",			H_From,			1, 0, 0, 0, 0)
HTTPH("Host",			H_Host,			1, 0, 0, 0, 0)
HTTPH("If-Match",		H_If_Match,		1, 0, 0, 0, 0)
HTTPH("If-Modified-Since",	H_If_Modified_Since,	1, 0, 0, 0, 0)
HTTPH("If-None-Match",		H_If_None_Match,	1, 0, 0, 0, 0)
HTTPH("If-Range",		H_If_Range,		1, 0, 0, 0, 0)
HTTPH("If-Unmodified-Since",	H_If_Unmodifed_Since,	1, 0, 0, 0, 0)
HTTPH("Keep-Alive",		H_Keep_Alive,		0, 0, 0, 0, 0)
HTTPH("Max-Forwards",		H_Max_Forwards,		1, 0, 0, 0, 0)
HTTPH("Proxy-Authorization",	H_Proxy_Authorization,	1, 0, 0, 0, 0)
HTTPH("Range",			H_Range,		1, 0, 0, 0, 0)
HTTPH("Referer",		H_Referer,		1, 0, 0, 0, 0)
HTTPH("TE",			H_TE,			1, 0, 0, 0, 0)
HTTPH("User-Agent",		H_User_Agent,		1, 0, 0, 0, 0)
