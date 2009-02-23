/*-
 * Copyright (c) 2008-2009 Linpro AS
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
 * Persistent storage method
 */

#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/mman.h>

#include "cache.h"
#include "stevedore.h"
#include "vsha256.h"

#include "persistent.h"

struct smp_sc {
	unsigned		magic;
#define SMP_SC_MAGIC		0x7b73af0a 

	int			fd;
	const char		*filename;
	off_t			mediasize;
	unsigned		granularity;

	uint8_t			*ptr;
};

/*--------------------------------------------------------------------*/

static void
smp_make_sign(const void *ptr, off_t len, uint8_t *dest)
{
	struct SHA256Context c;

	SHA256_Init(&c);
	SHA256_Update(&c, ptr, len);
	SHA256_Final(dest, &c);
}

/*--------------------------------------------------------------------*/

static int
smp_check_sign(const void *ptr, off_t len, const void *sign)
{
	struct SHA256Context c;
	unsigned char nsign[32];

	SHA256_Init(&c);
	SHA256_Update(&c, ptr, len);
	SHA256_Final(nsign, &c);
	return(memcmp(sign, nsign, sizeof nsign));
}

/*--------------------------------------------------------------------*/

static void
smp_newsilo(struct smp_sc *sc)
{
	struct smp_ident	*si;

	assert(strlen(SMP_IDENT_STRING) < sizeof si->ident);
	si = (void*)sc->ptr;
	memset(si, 0, sizeof *si);
	strcpy(si->ident, SMP_IDENT_STRING);
	si->byte_order = 0x12345678;
	si->size = sizeof *si;
	si->major_version = 1;
	si->minor_version = 1;
	si->mediasize = sc->mediasize;
	si->granularity = sc->granularity;

	smp_make_sign(si, sizeof *si, sc->ptr + sizeof *si);
}

/*--------------------------------------------------------------------*/

static int
smp_valid_ident(struct smp_sc *sc)
{
	struct smp_ident	*si;

	assert(strlen(SMP_IDENT_STRING) < sizeof si->ident);
	si = (void*)sc->ptr;
	if (strcmp(si->ident, SMP_IDENT_STRING))
		return (1);
	if (si->byte_order != 0x12345678)
		return (2);
	if (si->size != sizeof *si)
		return (3);
	if (smp_check_sign(si, sizeof *si, sc->ptr + sizeof *si))
		return (4);
	if (si->major_version != 1)
		return (5);
	if (si->minor_version != 1)
		return (6);
	if (si->mediasize != sc->mediasize)
		return (7);
	if (si->granularity != sc->granularity)
		return (8);
	return (0);
}

/*--------------------------------------------------------------------*/

static void
smp_init(struct stevedore *parent, int ac, char * const *av)
{
	struct smp_sc		*sc;
	int i;
	
	(void)parent;

	AZ(av[ac]);
	assert(sizeof(struct smp_ident) == SMP_IDENT_SIZE);
	assert(sizeof(struct smp_object) == SMP_OBJECT_SIZE);

	/* Allocate softc */
	ALLOC_OBJ(sc, SMP_SC_MAGIC);
	XXXAN(sc);
	sc->fd = -1;

	/* Argument processing */
	if (ac != 2)
		ARGV_ERR("(-spersistent) wrong number of arguments\n");

	i = STV_GetFile(av[0], &sc->fd, &sc->filename, "-spersistent");
	if (i == 2)
		ARGV_ERR("(-spersistent) need filename (not directory)\n");

	sc->granularity = getpagesize();
	sc->mediasize = STV_FileSize(sc->fd, av[1], &sc->granularity,
	    "-spersistent");

	AZ(ftruncate(sc->fd, sc->mediasize));

	sc->ptr = mmap(NULL, sc->mediasize, PROT_READ|PROT_WRITE,
	    MAP_NOCORE | MAP_NOSYNC | MAP_SHARED, sc->fd, 0);

	if (sc->ptr == MAP_FAILED)
		ARGV_ERR("(-spersistent) failed to mmap (%s)\n",
		    strerror(errno));

	fprintf(stderr, "i = %d ms = %jd g = %u\n",
	    i, (intmax_t)sc->mediasize, sc->granularity);

	fprintf(stderr, "Silo: %d\n", smp_valid_ident(sc));
	smp_newsilo(sc);
	fprintf(stderr, "Silo: %d\n", smp_valid_ident(sc));
	exit (2);
}

/*--------------------------------------------------------------------*/

struct stevedore smp_stevedore = {
	.magic	=	STEVEDORE_MAGIC,
	.name	=	"persistent",
	.init	=	smp_init,
	// .open	=	smf_open,
	// .alloc	=	smf_alloc,
	// .trim	=	smf_trim,
	// .free	=	smf_free,
};
