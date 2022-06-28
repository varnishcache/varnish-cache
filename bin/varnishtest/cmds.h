/*-
 * Copyright (c) 2018 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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

#ifndef CMD_GLOBAL
  #define CMD_GLOBAL(x)
#endif
CMD_GLOBAL(barrier)
CMD_GLOBAL(delay)
CMD_GLOBAL(shell)
#undef CMD_GLOBAL

#ifndef CMD_TOP
  #define CMD_TOP(x)
#endif
CMD_TOP(client)
CMD_TOP(feature)
CMD_TOP(filewrite)
CMD_TOP(haproxy)
#ifdef VTEST_WITH_VTC_LOGEXPECT
CMD_TOP(logexpect)
#endif
CMD_TOP(process)
CMD_TOP(server)
CMD_TOP(setenv)
CMD_TOP(syslog)
CMD_TOP(tunnel)
#ifdef VTEST_WITH_VTC_VARNISH
CMD_TOP(varnish)
#endif
CMD_TOP(varnishtest)
CMD_TOP(vtest)
#undef CMD_TOP

/*lint -restore */
