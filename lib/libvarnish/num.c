/*-
 * Copyright (c) 2008 Linpro AS
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
 *
 * Deal with numbers with data storage suffix scaling
 */

#include <ctype.h>
#include <stdio.h>
#include <stdint.h>

#include <libvarnish.h>

const char *
str2bytes(const char *p, uintmax_t *r, uintmax_t rel)
{
	int i;
	double l;
	char suff[2];

	i = sscanf(p, "%lg%1s", &l, suff);

	assert(i >= -1 && i <= 2);

	if (i < 1)
		return ("Could not find any number");

	if (l < 0.0)
		return ("Negative numbers not allowed");

	if (i == 2) {
		switch (tolower(*suff)) {
		case 'b': break;
		case 'k': l *= ((uintmax_t)1 << 10); break;
		case 'm': l *= ((uintmax_t)1 << 20); break;
		case 'g': l *= ((uintmax_t)1 << 30); break;
		case 't': l *= ((uintmax_t)1 << 40); break;
		case 'p': l *= ((uintmax_t)1 << 50); break;
		case 'e': l *= ((uintmax_t)1 << 60); break;
		case '%':
			/* Percentage of 'rel' arg */
			if (rel != 0) {
				l *= 1e-2 * rel;
				break;
			}
			/*FALLTHROUGH*/
		default:
			return ("Unknown scaling suffix [bkmgtpe] allowed");
		}
	}
	*r = (uintmax_t)(l + .5);
	return (NULL);
}

