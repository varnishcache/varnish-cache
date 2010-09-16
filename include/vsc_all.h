/*-
 * Copyright (c) 2010 Linpro AS
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
 */



VSC_DO(LCK, lck, VSC_TYPE_LCK)
#define VSC_DO_LCK
#include "vsc_fields.h"
#undef VSC_DO_LCK
VSC_DONE(LCK, lck, VSC_TYPE_LCK)

VSC_DO(MAIN, main, VSC_TYPE_MAIN)
#define VSC_DO_MAIN
#include "vsc_fields.h"
#undef VSC_DO_MAIN
VSC_DONE(MAIN, main, VSC_TYPE_MAIN)

VSC_DO(SMA, sma, VSC_TYPE_SMA)
#define VSC_DO_SMA
#include "vsc_fields.h"
#undef VSC_DO_SMA
VSC_DONE(SMA, sma, VSC_TYPE_SMA)

VSC_DO(SMF, smf, VSC_TYPE_SMF)
#define VSC_DO_SMF
#include "vsc_fields.h"
#undef VSC_DO_SMF
VSC_DONE(SMF, smf, VSC_TYPE_SMF)

VSC_DO(VBE, vbe, VSC_TYPE_VBE)
#define VSC_DO_VBE
#include "vsc_fields.h"
#undef VSC_DO_VBE
VSC_DONE(VBE, vbe, VSC_TYPE_VBE)
