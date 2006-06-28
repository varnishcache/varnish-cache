/*
 * $Id$
 *
 * Define the tags in the shared memory in a reusable format.
 * Whoever includes this get to define what the SLTM macro does.
 *
 */

SLTM(Debug)
SLTM(Error)
SLTM(CLI)
SLTM(SessionOpen)
SLTM(SessionReuse)
SLTM(SessionClose)
SLTM(BackendOpen)
SLTM(BackendReuse)
SLTM(BackendClose)
SLTM(HttpError)
SLTM(ClientAddr)
#define VCL_RET_MAC(l,u,b)
#define VCL_MET_MAC(l,u,b) SLTM(vcl_##l)
#include "vcl_returns.h"
#undef VCL_MET_MAC
#undef VCL_RET_MAC
SLTM(Backend)
SLTM(Request)
SLTM(Response)
SLTM(Status)
SLTM(URL)
SLTM(Protocol)
SLTM(Header)
SLTM(BldHdr)
SLTM(LostHeader)
SLTM(VCL)
