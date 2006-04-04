/*
 * $Id$
 *
 * a	Http header name
 * b	session field name
 * c	Request(1)/Response(2) bitfield
 * d	Pass header
 * e	unused
 * f	unused
 * g	unused
 *
 *    a                         b                       c  d  e  f  g 
 *--------------------------------------------------------------------
 */
HTTPH("Connection",		H_Connection,		3, 0, 0, 0, 0)
HTTPH("Keep-Alive",		H_Keep_Alive,		3, 0, 0, 0, 0)
HTTPH("Cache-Control",		H_Cache_Control,	3, 1, 0, 0, 0)

HTTPH("Accept-Charset",		H_Accept_Charset,	1, 1, 0, 0, 0)
HTTPH("Accept-Encoding",	H_Accept_Encoding,	1, 1, 0, 0, 0)
HTTPH("Accept-Language",	H_Accept_Language,	1, 1, 0, 0, 0)
HTTPH("Accept",			H_Accept,		1, 1, 0, 0, 0)
HTTPH("Authorization",		H_Authorization,	1, 1, 0, 0, 0)
HTTPH("Expect",			H_Expect,		1, 1, 0, 0, 0)
HTTPH("From",			H_From,			1, 1, 0, 0, 0)
HTTPH("Host",			H_Host,			1, 1, 0, 0, 0)
HTTPH("If-Match",		H_If_Match,		1, 1, 0, 0, 0)
HTTPH("If-Modified-Since",	H_If_Modified_Since,	1, 1, 0, 0, 0)
HTTPH("If-None-Match",		H_If_None_Match,	1, 1, 0, 0, 0)
HTTPH("If-Range",		H_If_Range,		1, 1, 0, 0, 0)
HTTPH("If-Unmodified-Since",	H_If_Unmodifed_Since,	1, 1, 0, 0, 0)
HTTPH("Max-Forwards",		H_Max_Forwards,		1, 1, 0, 0, 0)
HTTPH("Proxy-Authorization",	H_Proxy_Authorization,	1, 1, 0, 0, 0)
HTTPH("Range",			H_Range,		1, 1, 0, 0, 0)
HTTPH("Referer",		H_Referer,		1, 1, 0, 0, 0)
HTTPH("TE",			H_TE,			1, 1, 0, 0, 0)
HTTPH("User-Agent",		H_User_Agent,		1, 1, 0, 0, 0)
HTTPH("Pragma",			H_Pragma,		1, 1, 0, 0, 0)

HTTPH("Server",			H_Server,		2, 1, 0, 0, 0)
HTTPH("Content-Type",		H_Content_Type,		2, 1, 0, 0, 0)
HTTPH("Date",			H_Date,			2, 1, 0, 0, 0)
HTTPH("Last-Modified",		H_Last_Modified,	2, 1, 0, 0, 0)
HTTPH("Accept-Ranges",		H_Accept_Ranges,	2, 1, 0, 0, 0)
HTTPH("Content-Length",		H_Content_Length,	2, 1, 0, 0, 0)
HTTPH("Vary",			H_Vary,			2, 1, 0, 0, 0)
HTTPH("Expires",		H_Expires,		2, 1, 0, 0, 0)
HTTPH("Location",		H_Location,		2, 1, 0, 0, 0)
