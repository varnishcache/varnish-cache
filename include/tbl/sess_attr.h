/*-
 * Copyright (c) 2015 Varnish Software AS
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
 * Session attributes should be stored as cheaply as possible in terms of
 * memory.  This stuff implements "small pointers", 16 bit offsets into
 * the session workspace, for these bits.
 */

/*lint -save -e525 -e539 */

//        upper           lower         type		    len
SESS_ATTR(REMOTE_ADDR,	  remote_addr,	struct suckaddr,    vsa_suckaddr_len)
SESS_ATTR(LOCAL_ADDR,	  local_addr,	struct suckaddr,    vsa_suckaddr_len)
SESS_ATTR(CLIENT_ADDR,	  client_addr,	struct suckaddr,    vsa_suckaddr_len)
SESS_ATTR(SERVER_ADDR,	  server_addr,	struct suckaddr,    vsa_suckaddr_len)
SESS_ATTR(CLIENT_IP,	  client_ip,	char,		    -1)
SESS_ATTR(CLIENT_PORT,	  client_port,	char,		    -1)
SESS_ATTR(PROXY_TLV,	  proxy_tlv,	uintptr_t,	    sizeof(uintptr_t))
SESS_ATTR(PROTO_PRIV,	  proto_priv,	uintptr_t,	    sizeof(uintptr_t))
#undef SESS_ATTR

/*lint -restore */
