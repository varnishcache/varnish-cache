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
 *
 * XXX: Before we start the client or maybe after it stops, we should give the
 * XXX: stevedores a chance to examine their storage for consistency.
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
#include "hash_slinger.h"
#include "vsha256.h"

#include "persistent.h"

#ifndef MAP_NOCORE
#define MAP_NOCORE 0 /* XXX Linux */
#endif

#ifndef MAP_NOSYNC
#define MAP_NOSYNC 0 /* XXX Linux */
#endif

/* XXX: name confusion with on-media version ? */
struct smp_seg {
	unsigned		magic;
#define SMP_SEG_MAGIC		0x45c61895
	VTAILQ_ENTRY(smp_seg)	list;
	uint64_t		offset;
	uint64_t		length;
	struct smp_segment	segment;
	uint32_t		nalloc;
	uint32_t		maxobj;
	struct smp_object	*objs;
	uint64_t		next_addr;
};

VTAILQ_HEAD(smp_seghead, smp_seg);

struct smp_sc {
	unsigned		magic;
#define SMP_SC_MAGIC		0x7b73af0a 

	int			fd;
	const char		*filename;
	off_t			mediasize;
	unsigned		granularity;
	uint32_t		unique;

	uint8_t			*ptr;

	struct smp_ident	*ident;

	struct smp_seghead	segments;
	struct smp_seg		*cur_seg;
	pthread_t		thread;
};

/*--------------------------------------------------------------------
 * Write a sha256hash after a sequence of bytes.
 */

static void
smp_make_hash(void *ptr, off_t len)
{
	struct SHA256Context c;
	unsigned char *dest;

	dest = ptr;
	dest += len;
	SHA256_Init(&c);
	SHA256_Update(&c, ptr, len);
	SHA256_Final(dest, &c);
}

/*--------------------------------------------------------------------
 * Check that a sequence of bytes matches the SHA256 stored behind it.
 */

static int
smp_check_hash(void *ptr, off_t len)
{
	struct SHA256Context c;
	unsigned char sign[SHA256_LEN];
	unsigned char *dest;

	dest = ptr;
	dest += len;
	SHA256_Init(&c);
	SHA256_Update(&c, ptr, len);
	SHA256_Final(sign, &c);
	return(memcmp(sign, dest, sizeof sign));
}

/*--------------------------------------------------------------------
 * Create or write a signature block covering a sequence of bytes.
 */

static void
smp_create_sign(const struct smp_sc *sc, uint64_t adr, uint64_t len, const char *id)
{
	struct smp_sign *ss;

	AZ(adr & 0x7);			/* Enforce alignment */

	ss = (void*)(sc->ptr + adr);
	memset(ss, 0, sizeof *ss);
	assert(strlen(id) < sizeof ss->ident);
	strcpy(ss->ident, id);
	ss->unique = sc->unique;
	ss->mapped = (uintptr_t)(sc->ptr + adr);
	ss->length = len;
	smp_make_hash(ss, sizeof *ss + len);
fprintf(stderr, "CreateSign(%jx, %jx, %s)\n",
    adr, len, id);
}

/*--------------------------------------------------------------------
 * Force a write of a signature block to the backing store.
 */

static void
smp_sync_sign(const struct smp_sc *sc, uint64_t adr, uint64_t len)
{

	AZ(adr & 0x7);			/* Enforce alignment */

	AZ(msync(sc->ptr + adr,
	    sizeof(struct smp_sign) + len + SHA256_LEN, MS_SYNC));
fprintf(stderr, "SyncSign(%jx, %jx)\n", adr, len);
}

/*--------------------------------------------------------------------
 * Check a signature block and return zero if OK.
 */

static int
smp_check_sign(const struct smp_sc *sc, uint64_t adr, const char *id)
{
	struct smp_sign *ss;

	AZ(adr & 0x7);			/* Enforce alignment */

	ss = (void*)(sc->ptr + adr);
	assert(strlen(id) < sizeof ss->ident);
	if (strcmp(id, ss->ident))
		return (1);
	if (ss->unique != sc->unique)
		return (2);
	if (ss->mapped != (uintptr_t)ss)
		return (3);
	return (smp_check_hash(ss, sizeof *ss + ss->length));
}

/*--------------------------------------------------------------------
 * Open a signature block, and return zero if it is valid.
 */

static int
smp_open_sign(const struct smp_sc *sc, uint64_t adr, void **ptr, uint64_t *len, const char *id)
{
	struct smp_sign *ss;
	int i;
	
	AZ(adr & 0x7);			/* Enforce alignment */
	AN(ptr);
	AN(len);
	ss = (void*)(sc->ptr + adr);
	*ptr = (void*)(sc->ptr + adr + sizeof *ss);
	*len = ss->length;
	i = smp_check_sign(sc, adr, id);
fprintf(stderr, "OpenSign(%jx, %s) -> {%p, %jx, %d}\n",
    adr, id, *ptr, *len, i);
	return (i);
}

/*--------------------------------------------------------------------
 * Initialize a Silo with a valid but empty structure.
 *
 * XXX: more intelligent sizing of things.
 */

static void
smp_newsilo(struct smp_sc *sc)
{
	struct smp_ident	*si;

	assert(strlen(SMP_IDENT_STRING) < sizeof si->ident);

	sc->unique = random();

	si = (void*)sc->ptr;
	memset(si, 0, sizeof *si);
	strcpy(si->ident, SMP_IDENT_STRING);
	si->byte_order = 0x12345678;
	si->size = sizeof *si;
	si->major_version = 1;
	si->minor_version = 1;
	si->unique = sc->unique;
	si->mediasize = sc->mediasize;
	si->granularity = sc->granularity;

	si->stuff[SMP_BAN1_STUFF] = sc->granularity;
	si->stuff[SMP_BAN2_STUFF] = si->stuff[SMP_BAN1_STUFF] + 1024*1024;
	si->stuff[SMP_SEG1_STUFF] = si->stuff[SMP_BAN2_STUFF] + 1024*1024;
	si->stuff[SMP_SEG2_STUFF] = si->stuff[SMP_SEG1_STUFF] + 1024*1024;
	si->stuff[SMP_SPC_STUFF] = si->stuff[SMP_SEG2_STUFF] + 1024*1024;
	si->stuff[SMP_END_STUFF] = si->mediasize;
	smp_create_sign(sc, si->stuff[SMP_BAN1_STUFF], 0, "BAN 1");
	smp_create_sign(sc, si->stuff[SMP_BAN2_STUFF], 0, "BAN 2");
	smp_create_sign(sc, si->stuff[SMP_SEG1_STUFF], 0, "SEG 1");
	smp_create_sign(sc, si->stuff[SMP_SEG2_STUFF], 0, "SEG 2");

	smp_make_hash(si, sizeof *si);
}

/*--------------------------------------------------------------------
 * Check if a silo is valid.
 */

static int
smp_valid_silo(struct smp_sc *sc)
{
	struct smp_ident	*si;
	int i, j;

	assert(strlen(SMP_IDENT_STRING) < sizeof si->ident);
	si = (void*)sc->ptr;
	if (strcmp(si->ident, SMP_IDENT_STRING))
		return (1);
	if (si->byte_order != 0x12345678)
		return (2);
	if (si->size != sizeof *si)
		return (3);
	if (smp_check_hash(si, sizeof *si))
		return (4);
	if (si->major_version != 1)
		return (5);
	if (si->minor_version != 1)
		return (6);
	if (si->mediasize != sc->mediasize)
		return (7);
	if (si->granularity != sc->granularity)
		return (8);
	sc->unique = si->unique;

	/* XXX: Sanity check stuff[4] */

	assert(si->stuff[SMP_BAN1_STUFF] > sizeof *si + SHA256_LEN);
	assert(si->stuff[SMP_BAN2_STUFF] > si->stuff[0]);
	assert(si->stuff[SMP_SEG1_STUFF] > si->stuff[1]);
	assert(si->stuff[SMP_SEG2_STUFF] > si->stuff[2]);
	assert(si->stuff[SMP_SPC_STUFF] > si->stuff[3]);
	assert(si->stuff[SMP_END_STUFF] == sc->mediasize);

	/* We must have one valid BAN table */
	i = smp_check_sign(sc, si->stuff[SMP_BAN1_STUFF], "BAN 1");
	j = smp_check_sign(sc, si->stuff[SMP_BAN2_STUFF], "BAN 2");
	if (i && j)
		return (100 + i * 10 + j);

	/* We must have one valid SEG table */
	i = smp_check_sign(sc, si->stuff[SMP_SEG1_STUFF], "SEG 1");
	j = smp_check_sign(sc, si->stuff[SMP_SEG2_STUFF], "SEG 2");
	if (i && j)
		return (200 + i * 10 + j);
	return (0);
}

/*--------------------------------------------------------------------
 * Set up persistent storage silo in the master process.
 */

static void
smp_init(struct stevedore *parent, int ac, char * const *av)
{
	struct smp_sc		*sc;
	int i;
	
	(void)parent;

	AZ(av[ac]);
#define SIZOF(foo)       fprintf(stderr, \
    "sizeof(%s) = %zu = 0x%zx\n", #foo, sizeof(foo), sizeof(foo));
	SIZOF(struct smp_ident);
	SIZOF(struct smp_sign);
	SIZOF(struct smp_segptr);
	SIZOF(struct smp_object);
#undef SIZOF

	assert(sizeof(struct smp_ident) == SMP_IDENT_SIZE);
	assert(sizeof(struct smp_sign) == SMP_SIGN_SIZE);
	assert(sizeof(struct smp_object) == SMP_OBJECT_SIZE);
	/* XXX: assert(sizeof smp_object.hash == DIGEST_LEN); */

	/* Allocate softc */
	ALLOC_OBJ(sc, SMP_SC_MAGIC);
	XXXAN(sc);
	sc->fd = -1;
	VTAILQ_INIT(&sc->segments);

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

	i = smp_valid_silo(sc);
	fprintf(stderr, "Silo: %d\n", i);
	if (i)
		smp_newsilo(sc);
	fprintf(stderr, "Silo: %d\n", smp_valid_silo(sc));
	AZ(smp_valid_silo(sc));

	parent->priv = sc;

	/* XXX: only for sendfile I guess... */
	mgt_child_inherit(sc->fd, "storage_persistent");
}

/*--------------------------------------------------------------------
 * Write the segmentlist back to the silo.
 *
 * We write the first copy, sync it synchronously, then write the
 * second copy and sync it synchronously.
 *
 * Provided the kernel doesn't lie, that means we will always have
 * at least one valid copy on in the silo.
 */

static void
smp_save_seg(struct smp_sc *sc, uint64_t adr, const char *id)
{
	struct smp_segptr *ss;
	struct smp_seg *sg;
	void *ptr;
	uint64_t length;

	(void)smp_open_sign(sc, adr, &ptr, &length, id);
	ss = ptr;
	length = 0;
	VTAILQ_FOREACH(sg, &sc->segments, list) {
		ss->offset = sg->offset;
		ss->length = sg->length;
		ss++;
		length += sizeof *ss;
fprintf(stderr, "WR SEG %jx %jx\n", sg->offset, sg->length);
	}
	smp_create_sign(sc, adr, length, id);
	smp_sync_sign(sc, adr, length);
}

static void
smp_save_segs(struct smp_sc *sc)
{

	smp_save_seg(sc, sc->ident->stuff[SMP_SEG1_STUFF], "SEG 1");
	smp_save_seg(sc, sc->ident->stuff[SMP_SEG2_STUFF], "SEG 2");
}

/*--------------------------------------------------------------------
 * Fixup an object
 */

void
SMP_Fixup(struct sess *sp, struct objhead *oh, struct objcore *oc)
{
	struct smp_object *so;

fprintf(stderr, "Fixup %p %p\n", sp, oc);

	so = (void*)oc->obj;
	oc->obj = so->ptr;

	/* XXX: This should fail gracefully */
	CHECK_OBJ_NOTNULL(oc->obj, OBJECT_MAGIC);

	oc->obj->smp = so;

	oc->flags &= ~OC_F_PERSISTENT;

	oc->obj->refcnt = 0;
	oc->obj->objcore = oc;
	oc->obj->objhead = oh;

	/* XXX: Placeholder for persistent bans */
	oc->obj->ban = NULL;
	BAN_NewObj(oc->obj);
}

/*--------------------------------------------------------------------
 * Update objects
 */

void
SMP_BANchanged(const struct object *o)
{
	assert(o->smp != NULL);
}

void
SMP_TTLchanged(const struct object *o)
{

	assert(o->smp != NULL);
}

/*--------------------------------------------------------------------
 * Load segments
 *
 * The overall objective is to register the existence of an object, based
 * only on the minimally sized struct smp_object, without causing the
 * main object to be faulted in.
 *
 * XXX: We can test this by mprotecting the main body of the segment
 * XXX: until the first fixup happens, or even just over this loop,
 * XXX: However: the requires that the smp_objects starter further
 * XXX: into the segment than a page so that they do not get hit
 * XXX: by the protection.
 */

static void
smp_load_seg(struct sess *sp, struct smp_sc *sc, struct smp_seg *sg)
{
	void *ptr;
	uint64_t length;
	struct smp_segment *ss;
	struct smp_object *so;
	uint32_t no;
	double t_now = TIM_real();

	(void)sp;
	if (smp_open_sign(sc, sg->offset, &ptr, &length, "SEGMENT"))
		return;
	fprintf(stderr, "Load Seg %p %jx\n", ptr, length);
	ss = ptr;
	fprintf(stderr, "Objlist %jx Nalloc %u\n", ss->objlist, ss->nalloc);
	so = (void*)(sc->ptr + ss->objlist);
	no = ss->nalloc;
	for (;no > 0; so++,no--) {
		if (so->ttl < t_now)
			continue;
		fprintf(stderr, "OBJ %p dTTL: %g PTR %p\n",
		    so, so->ttl - t_now, so->ptr);
		HSH_Prealloc(sp);
		sp->wrk->nobjcore->flags |= OC_F_PERSISTENT;
		sp->wrk->nobjcore->flags &= ~OC_F_BUSY;
		sp->wrk->nobjcore->obj = (void*)so;
		memcpy(sp->wrk->nobjhead->digest, so->hash, SHA256_LEN);
		(void)HSH_Insert(sp);
	}
}

/*--------------------------------------------------------------------
 * Attempt to open and read in a segment list
 */

static int
smp_open_segs(struct smp_sc *sc, int stuff, const char *id)
{
	void *ptr;
	uint64_t length;
	struct smp_segptr *ss;
	struct smp_seg *sg;

	if (smp_open_sign(sc, sc->ident->stuff[stuff], &ptr, &length, id))
		return (1);

	ss = ptr;
	for(; length > 0; length -= sizeof *ss, ss ++) {
		ALLOC_OBJ(sg, SMP_SEG_MAGIC);
		AN(sg);
		sg->offset = ss->offset;
		sg->length = ss->length;
		VTAILQ_INSERT_TAIL(&sc->segments, sg, list);
fprintf(stderr, "RD SEG %jx %jx\n", sg->offset, sg->length);
	}
	return (0);
}

/*--------------------------------------------------------------------
 * Create a new segment
 */

static void
smp_new_seg(struct smp_sc *sc)
{
	struct smp_seg *sg, *sg2;
	void *ptr;
	uint64_t length;

	ALLOC_OBJ(sg, SMP_SEG_MAGIC);
	AN(sg);

	sg->maxobj = 16;		/* XXX: param ? */
	sg->objs = malloc(sizeof *sg->objs * sg->maxobj);
	AN(sg->objs);

	/* XXX: find where it goes in silo */

	sg2 = VTAILQ_LAST(&sc->segments, smp_seghead);
	if (sg2 == NULL)
		sg->offset = sc->ident->stuff[SMP_SPC_STUFF];
	else
		sg->offset = sg2->offset + sg2->length;
	sg->length = sc->ident->stuff[SMP_END_STUFF] - sg->offset;

	VTAILQ_INSERT_TAIL(&sc->segments, sg, list);
fprintf(stderr, "MK SEG %jx %jx\n", sg->offset, sg->length);

	/* Neuter the new segment in case there is an old one there */
	(void)smp_open_sign(sc, sg->offset, &ptr, &length, "SEGMENT");
	memcpy(ptr, &sg->segment, sizeof sg->segment);
	smp_create_sign(sc, sg->offset, sizeof sg->segment, "SEGMENT");
	smp_sync_sign(sc, sg->offset, sizeof sg->segment);

	/* Then add it to the segment list. */
	smp_save_segs(sc);

	/* Set up our allocation point */
	sc->cur_seg = sg;
	sg->next_addr = sg->offset +
	    sizeof (struct smp_sign) +
	    sizeof (struct smp_segment) +
	    SHA256_LEN;
	memcpy(sc->ptr + sg->next_addr, "HERE", 4);
}

/*--------------------------------------------------------------------
 * Close a segment
 */

static void
smp_close_seg(struct smp_sc *sc, struct smp_seg *sg)
{
	void *ptr;
	uint64_t length;

	(void)sc;
	/* XXX: if segment is empty, delete instead */
fprintf(stderr, "Close seg %p na = %jx\n", sg, sg->next_addr);
	/* Copy the objects into the segment */
	memcpy(sc->ptr + sg->next_addr,
	    sg->objs, sizeof *sg->objs * sg->nalloc);

	/* Update the segment header */
	sg->segment.objlist = sg->next_addr;
	sg->segment.nalloc = sg->nalloc;

	/* Write it to silo */
	(void)smp_open_sign(sc, sg->offset, &ptr, &length, "SEGMENT");
	memcpy(ptr, &sg->segment, sizeof sg->segment);
	smp_create_sign(sc, sg->offset, sizeof sg->segment, "SEGMENT");

	sg->next_addr += sizeof *sg->objs * sg->nalloc;
	sg->length = sg->next_addr - sg->offset;
	sg->length |= 7;
	sg->length++;

	/* Save segment list */
	smp_save_segs(sc);

}

/*--------------------------------------------------------------------
 * Silo worker thread
 */

static void *
smp_thread(struct sess *sp, void *priv)
{
	struct smp_sc	*sc;
	struct smp_seg *sg;

	(void)sp;
	CAST_OBJ_NOTNULL(sc, priv, SMP_SC_MAGIC);

	VTAILQ_FOREACH(sg, &sc->segments, list)
		smp_load_seg(sp, sc, sg);

	while (1)	
		sleep (1);
	return (NULL);
}

/*--------------------------------------------------------------------
 * Open a silo in the worker process
 */

static void
smp_open(const struct stevedore *st)
{
	struct smp_sc	*sc;

	CAST_OBJ_NOTNULL(sc, st->priv, SMP_SC_MAGIC);
fprintf(stderr, "Open Silo(%p)\n", st);

	/* We trust the parent to give us a valid silo, for good measure: */
	AZ(smp_valid_silo(sc));

	sc->ident = (void*)sc->ptr;

	/* XXX: read in bans */

	/*
	 * We attempt seg1 first, and if that fails, try seg2
	 */
	if (smp_open_segs(sc, SMP_SEG1_STUFF, "SEG 1"))
		AZ(smp_open_segs(sc, SMP_SEG2_STUFF, "SEG 2"));

	/* Open a new segment, so we are ready to write */
	smp_new_seg(sc);

	WRK_BgThread(&sc->thread, "persistence", smp_thread, sc);
}

/*--------------------------------------------------------------------
 * Close a silo
 */

static void
smp_close(const struct stevedore *st)
{
	struct smp_sc	*sc;

	CAST_OBJ_NOTNULL(sc, st->priv, SMP_SC_MAGIC);
fprintf(stderr, "Close Silo(%p)\n", st);
	smp_close_seg(sc, sc->cur_seg);
}

/*--------------------------------------------------------------------
 * Designate object
 */

static void
smp_object(const struct sess *sp)
{
	struct smp_sc	*sc;
	struct smp_seg *sg;
	struct smp_object *so;

	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj->objstore, STORAGE_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj->objstore->stevedore, STEVEDORE_MAGIC);
	CAST_OBJ_NOTNULL(sc, sp->obj->objstore->priv, SMP_SC_MAGIC);

	sg = sc->cur_seg;
	so = &sg->objs[sg->nalloc++];
	memcpy(so->hash, sp->obj->objhead->digest, DIGEST_LEN);
	so->ttl = sp->obj->ttl;
	so->ptr = sp->obj;

fprintf(stderr, "Object(%p %p)\n", sp, sp->obj);
}

/*--------------------------------------------------------------------
 * Allocate a bite
 */

static struct storage *
smp_alloc(struct stevedore *st, size_t size)
{
	struct smp_sc *sc;
	struct storage *ss;
	struct smp_seg *sg;

	CAST_OBJ_NOTNULL(sc, st->priv, SMP_SC_MAGIC);
	sg = sc->cur_seg;

	/* XXX: size fit check */
	AN(sg->next_addr);

	/* Grab and fill a storage structure */
	ss = (void *)(sc->ptr + sg->next_addr);
	memset(ss, 0, sizeof *ss);
	ss->magic = STORAGE_MAGIC;
	ss->space = size;
	ss->ptr = (void *)(ss + 1);
	ss->priv = sc;
	ss->stevedore = st;
	ss->fd = sc->fd;
	ss->where = sg->next_addr + sizeof *ss;

	sg->next_addr += size + sizeof *ss;
	memcpy(sc->ptr + sg->next_addr, "HERE", 4);
	return (ss);
}

static void
smp_trim(struct storage *ss, size_t size)
{
	struct smp_sc *sc;
	struct smp_seg *sg;

fprintf(stderr, "Trim(%p %u)\n", ss, size);
	CAST_OBJ_NOTNULL(sc, ss->priv, SMP_SC_MAGIC);
	sg = sc->cur_seg;

	/* We want 16 bytes alignment */
	size |= 0xf;
	size += 1;

	if (ss->ptr + ss->space == sg->next_addr + sc->ptr) {
		memcpy(sc->ptr + sg->next_addr, "\0\0\0\0", 4);
		sg->next_addr -= ss->space - size;
		ss->space = size;
		memcpy(sc->ptr + sg->next_addr, "HERE", 4);
	}
}

static void
smp_free(struct storage *st)
{

	/* XXX */
	(void)st;
}


/*--------------------------------------------------------------------*/

struct stevedore smp_stevedore = {
	.magic	=	STEVEDORE_MAGIC,
	.name	=	"persistent",
	.init	=	smp_init,
	.open	=	smp_open,
	.close	=	smp_close,
	.alloc	=	smp_alloc,
	.object	=	smp_object,
	.free	=	smp_free,
	.trim	=	smp_trim,
};
