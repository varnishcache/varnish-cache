/*-
 * Copyright (c) 2013-2015 Varnish Software AS
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

#ifndef VSA_H_INCLUDED
#define VSA_H_INCLUDED

struct suckaddr;
extern const int vsa_suckaddr_len;
extern const struct suckaddr *bogo_ip;

void VSA_Init(void);
int VSA_Sane(const struct suckaddr *);
unsigned VSA_Port(const struct suckaddr *);
int VSA_Compare(const struct suckaddr *, const struct suckaddr *);
int VSA_Compare_IP(const struct suckaddr *, const struct suckaddr *);
struct suckaddr *VSA_Clone(const struct suckaddr *sua);

const void *VSA_Get_Sockaddr(const struct suckaddr *, socklen_t *sl);
int VSA_Get_Proto(const struct suckaddr *);

/*
 * 's' is a sockaddr of some kind, 'sal' is its length
 */
struct suckaddr *VSA_Malloc(const void *s, unsigned  sal);

/*
 * 'd' SHALL point to vsa_suckaddr_len aligned bytes of storage,
 * 's' is a sockaddr of some kind, 'sal' is its length.
 */
struct suckaddr *VSA_Build(void *d, const void *s, unsigned sal);

/*
 * This VRT interface is for the VCC generated ACL code, which needs
 * to know the address family and a pointer to the actual address.
 */

int VSA_GetPtr(const struct suckaddr *sua, const unsigned char ** dst);

#endif
