/*
 * $Id$
 *
 * Define the tags in the shared memory in a reusable format.
 * Whoever includes this get to define what the SLTM macro does.
 *
 * REMEMBER to update the documentation (especially the varnishlog(1) man
 * page) whenever this list changes.
 */

SLTM(Debug)
SLTM(Error)
SLTM(CLI)
SLTM(SessionOpen)
SLTM(SessionReuse)
SLTM(SessionClose)
SLTM(BackendOpen)
SLTM(BackendXID)
SLTM(BackendReuse)
SLTM(BackendClose)
SLTM(HttpError)
SLTM(ClientAddr)
SLTM(Backend)
SLTM(Request)
SLTM(Response)
SLTM(Length)
SLTM(Status)
SLTM(URL)
SLTM(Protocol)
SLTM(Header)
SLTM(BldHdr)
SLTM(LostHeader)
SLTM(VCL_call)
SLTM(VCL_trace)
SLTM(VCL_return)
SLTM(XID)
SLTM(Hit)
SLTM(ExpBan)
SLTM(ExpPick)
SLTM(ExpKill)
SLTM(WorkThread)
