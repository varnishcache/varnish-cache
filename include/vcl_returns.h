/*
 * $Id$
 *
 * NB:  This file is machine generated, DO NOT EDIT!
 *
 * Edit and run vcc_gen_fixed_token.tcl instead
 */

#ifdef VCL_RET_MAC
VCL_RET_MAC(error, ERROR)
VCL_RET_MAC(lookup, LOOKUP)
VCL_RET_MAC(hash, HASH)
VCL_RET_MAC(pipe, PIPE)
VCL_RET_MAC(pass, PASS)
VCL_RET_MAC(fetch, FETCH)
VCL_RET_MAC(deliver, DELIVER)
VCL_RET_MAC(discard, DISCARD)
VCL_RET_MAC(keep, KEEP)
VCL_RET_MAC(restart, RESTART)
#endif

#ifdef VCL_MET_MAC
VCL_MET_MAC(recv,RECV,
     ((1 << VCL_RET_ERROR)
    | (1 << VCL_RET_PASS)
    | (1 << VCL_RET_PIPE)
    | (1 << VCL_RET_LOOKUP)
))
VCL_MET_MAC(pipe,PIPE,
     ((1 << VCL_RET_ERROR)
    | (1 << VCL_RET_PIPE)
))
VCL_MET_MAC(pass,PASS,
     ((1 << VCL_RET_ERROR)
    | (1 << VCL_RET_RESTART)
    | (1 << VCL_RET_PASS)
))
VCL_MET_MAC(hash,HASH,
     ((1 << VCL_RET_HASH)
))
VCL_MET_MAC(miss,MISS,
     ((1 << VCL_RET_ERROR)
    | (1 << VCL_RET_RESTART)
    | (1 << VCL_RET_PASS)
    | (1 << VCL_RET_FETCH)
))
VCL_MET_MAC(hit,HIT,
     ((1 << VCL_RET_ERROR)
    | (1 << VCL_RET_RESTART)
    | (1 << VCL_RET_PASS)
    | (1 << VCL_RET_DELIVER)
))
VCL_MET_MAC(fetch,FETCH,
     ((1 << VCL_RET_ERROR)
    | (1 << VCL_RET_RESTART)
    | (1 << VCL_RET_PASS)
    | (1 << VCL_RET_DELIVER)
))
VCL_MET_MAC(deliver,DELIVER,
     ((1 << VCL_RET_RESTART)
    | (1 << VCL_RET_DELIVER)
))
VCL_MET_MAC(prefetch,PREFETCH,
     ((1 << VCL_RET_FETCH)
    | (1 << VCL_RET_PASS)
))
VCL_MET_MAC(timeout,TIMEOUT,
     ((1 << VCL_RET_FETCH)
    | (1 << VCL_RET_DISCARD)
))
VCL_MET_MAC(discard,DISCARD,
     ((1 << VCL_RET_DISCARD)
    | (1 << VCL_RET_KEEP)
))
VCL_MET_MAC(error,ERROR,
     ((1 << VCL_RET_DELIVER)
))
#endif
