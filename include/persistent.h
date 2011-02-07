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
 */

/*
 *
 * Overall layout:
 *
 *	struct smp_ident;		Identification and geometry
 *	sha256[...]			checksum of same
 *
 *	struct smp_sign;
 *	banspace_1;			First ban-space
 *	sha256[...]			checksum of same
 *
 *	struct smp_sign;
 *	banspace_2;			Second ban-space
 *	sha256[...]			checksum of same
 *
 *	struct smp_sign;
 *	struct smp_segment_1[N];	First Segment table
 *	sha256[...]			checksum of same
 *
 *	struct smp_sign;
 *	struct smp_segment_2[N];	Second Segment table
 *	sha256[...]			checksum of same
 *
 *	N segments {
 *		struct smp_sign;
 *		struct smp_object[M]	Objects in segment
 *		sha256[...]		checksum of same
 *		objspace
 *	}
 *
 */

/*
 * The identblock is located in the first sector of the storage space.
 * This is written once and not subsequently modified in normal operation.
 * It is immediately followed by a SHA256sum of the structure, as stored.
 */

struct smp_ident {
	char			ident[32];	/* Human readable ident
						 * so people and programs
						 * can tell what the file
						 * or device contains.
						 */

	uint32_t		byte_order;	/* 0x12345678 */

	uint32_t		size;		/* sizeof(struct smp_ident) */

	uint32_t		major_version;

	uint32_t		unique;

	uint32_t		align;		/* alignment in silo */

	uint32_t		granularity;	/* smallest ... in bytes */

	uint64_t		mediasize;	/* ... in bytes */

	uint64_t		stuff[6];	/* pointers to stuff */
#define	SMP_BAN1_STUFF		0
#define	SMP_BAN2_STUFF		1
#define	SMP_SEG1_STUFF		2
#define	SMP_SEG2_STUFF		3
#define	SMP_SPC_STUFF		4
#define	SMP_END_STUFF		5
};

/*
 * The size of smp_ident should be fixed and constant across all platforms.
 * We enforce that with the following #define and an assert in smp_init()
 */
#define SMP_IDENT_SIZE		112

#define SMP_IDENT_STRING	"Varnish Persistent Storage Silo"

/*
 * This is used to sign various bits on the disk.
 */

struct smp_sign {
	char			ident[8];
	uint32_t		unique;
	uint64_t		mapped;
	uint64_t		length;		/* NB: Must be last */
};

#define SMP_SIGN_SPACE		(sizeof(struct smp_sign) + SHA256_LEN)

/*
 * A segment pointer.
 */

struct smp_segptr {
	uint64_t		offset;		/* rel to silo */
	uint64_t		length;		/* rel to offset */
	uint64_t		objlist;	/* rel to silo */
	uint32_t		lobjlist;	/* len of objlist */
};

/*
 * An object descriptor
 *
 * A positive ttl is obj.ttl with obj.grace being NAN
 * A negative ttl is - (obj.ttl + obj.grace)
 */

struct smp_object {
	unsigned char		hash[32];
	double			ttl;
	double			ban;
	struct object		*ptr;
};
