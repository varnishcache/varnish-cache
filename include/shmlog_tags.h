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
SLTM(StatAddr)
SLTM(StatSess)
SLTM(ReqEnd)
SLTM(SessionOpen)
SLTM(SessionClose)
SLTM(BackendOpen)
SLTM(BackendXID)
SLTM(BackendReuse)
SLTM(BackendClose)
SLTM(HttpError)
SLTM(HttpGarbage)
SLTM(ClientAddr)
SLTM(Backend)
SLTM(Length)

SLTM(RxRequest)
SLTM(RxResponse)
SLTM(RxStatus)
SLTM(RxURL)
SLTM(RxProtocol)
SLTM(RxHeader)
SLTM(RxLostHeader)

SLTM(TxRequest)
SLTM(TxResponse)
SLTM(TxStatus)
SLTM(TxURL)
SLTM(TxProtocol)
SLTM(TxHeader)
SLTM(TxLostHeader)

SLTM(ObjRequest)
SLTM(ObjResponse)
SLTM(ObjStatus)
SLTM(ObjURL)
SLTM(ObjProtocol)
SLTM(ObjHeader)
SLTM(ObjLostHeader)

SLTM(TTL)
SLTM(VCL_acl)
SLTM(VCL_call)
SLTM(VCL_trace)
SLTM(VCL_return)
SLTM(ReqStart)
SLTM(Hit)
SLTM(HitPass)
SLTM(ExpBan)
SLTM(ExpPick)
SLTM(ExpKill)
SLTM(WorkThread)
