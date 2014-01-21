/*-
 * Copyright (c) 2010-2014 Varnish Software AS
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
 */


VSC_DO(MGT, mgt, VSC_type_mgt)
#define VSC_DO_MGT
#include "tbl/vsc_fields.h"
#undef VSC_DO_MGT
VSC_DONE(MGT, mgt, VSC_type_mgt)

VSC_DO(LCK, lck, VSC_type_lck)
#define VSC_DO_LCK
#include "tbl/vsc_fields.h"
#undef VSC_DO_LCK
VSC_DONE(LCK, lck, VSC_type_lck)

VSC_DO(MAIN, main, VSC_type_main)
#include "tbl/vsc_f_main.h"
VSC_DONE(MAIN, main, VSC_type_main)

VSC_DO(SMA, sma, VSC_type_sma)
#define VSC_DO_SMA
#include "tbl/vsc_fields.h"
#undef VSC_DO_SMA
VSC_DONE(SMA, sma, VSC_type_sma)

VSC_DO(SMF, smf, VSC_type_smf)
#define VSC_DO_SMF
#include "tbl/vsc_fields.h"
#undef VSC_DO_SMF
VSC_DONE(SMF, smf, VSC_type_smf)

VSC_DO(VBE, vbe, VSC_type_vbe)
#define VSC_DO_VBE
#include "tbl/vsc_fields.h"
#undef VSC_DO_VBE
VSC_DONE(VBE, vbe, VSC_type_vbe)

VSC_DO(MEMPOOL, mempool, VSC_type_mempool)
#define VSC_DO_MEMPOOL
#include "tbl/vsc_fields.h"
#undef VSC_DO_MEMPOOL
VSC_DONE(MEMPOOL, mempool, VSC_type_mempool)
