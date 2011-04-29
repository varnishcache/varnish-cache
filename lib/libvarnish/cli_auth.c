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
 */

#include "config.h"

#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>

#include "cli.h"
#include "cli_common.h"
#include "libvarnish.h"
#include "vsha256.h"


void
CLI_response(int S_fd, const char *challenge,
    char response[CLI_AUTH_RESPONSE_LEN])
{
	SHA256_CTX ctx;
	uint8_t buf[BUFSIZ];
	int i;

	assert(CLI_AUTH_RESPONSE_LEN == (SHA256_LEN * 2 + 1));

	SHA256_Init(&ctx);
	SHA256_Update(&ctx, challenge, 32);
	SHA256_Update(&ctx, "\n", 1);
	do {
		i = read(S_fd, buf, sizeof buf);
		if (i > 0)
			SHA256_Update(&ctx, buf, i);
	} while (i > 0);
	SHA256_Update(&ctx, challenge, 32);
	SHA256_Update(&ctx, "\n", 1);
	SHA256_Final(buf, &ctx);
	for(i = 0; i < SHA256_LEN; i++)
		sprintf(response + 2 * i, "%02x", buf[i]);
}
