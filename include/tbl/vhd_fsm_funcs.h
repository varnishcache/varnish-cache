/*-
 * Copyright (c) 2016 Varnish Software
 * All rights reserved.
 *
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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

/*lint -save -e525 -e539 */

VHD_FSM_FUNC(SKIP, vhd_skip)
VHD_FSM_FUNC(GOTO, vhd_goto)
VHD_FSM_FUNC(IDLE, vhd_idle)
VHD_FSM_FUNC(INTEGER, vhd_integer)
VHD_FSM_FUNC(SET_MAX, vhd_set_max)
VHD_FSM_FUNC(SET_IDX, vhd_set_idx)
VHD_FSM_FUNC(LOOKUP, vhd_lookup)
VHD_FSM_FUNC(NEW, vhd_new)
VHD_FSM_FUNC(NEW_IDX, vhd_new_idx)
VHD_FSM_FUNC(BRANCH_ZIDX, vhd_branch_zidx)
VHD_FSM_FUNC(BRANCH_BIT0, vhd_branch_bit0)
VHD_FSM_FUNC(RAW, vhd_raw)
VHD_FSM_FUNC(HUFFMAN, vhd_huffman)
#undef VHD_FSM_FUNC

/*lint -restore */
