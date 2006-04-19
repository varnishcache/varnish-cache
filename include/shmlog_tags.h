/*
 * $Id$
 *
 * Define the tags in the shared memory in a reusable format.
 * Whoever includes this get to define what the SLTM macro does.
 *
 */

SLTM(Debug)
SLTM(CLI)
SLTM(SessionOpen)
SLTM(SessionReuse)
SLTM(SessionClose)
SLTM(ClientAddr)
SLTM(HandlingFetch)
SLTM(HandlingPass)
SLTM(HandlingPipe)
SLTM(Request)
SLTM(Response)
SLTM(Status)
SLTM(URL)
SLTM(Protocol)
SLTM(HD_Unknown)
SLTM(HD_Lost)
#define HTTPH(a, b, c, d, e, f, g)	SLTM(b)
#include "http_headers.h"
#undef HTTPH
