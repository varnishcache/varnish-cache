/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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

#include "config.h"

#include <pcre.h>

#include "cache.h"
#include "cache_ban.h"

#include "hash/hash_slinger.h"
#include "vcli_serve.h"
#include "vend.h"
#include "vmb.h"

struct lock ban_mtx;
int ban_shutdown;
struct banhead_s ban_head = VTAILQ_HEAD_INITIALIZER(ban_head);
struct ban * volatile ban_start;

static pthread_t ban_thread;
static int ban_holds;
uint64_t bans_persisted_bytes;
uint64_t bans_persisted_fragmentation;

struct ban_test {
	uint8_t			oper;
	uint8_t			arg1;
	const char		*arg1_spec;
	const char		*arg2;
	const void		*arg2_spec;
};

/*--------------------------------------------------------------------
 * Storage handling of bans
 */

static struct ban *
ban_alloc(void)
{
	struct ban *b;

	ALLOC_OBJ(b, BAN_MAGIC);
	if (b != NULL)
		VTAILQ_INIT(&b->objcore);
	return (b);
}

void
BAN_Free(struct ban *b)
{

	CHECK_OBJ_NOTNULL(b, BAN_MAGIC);
	AZ(b->refcount);
	assert(VTAILQ_EMPTY(&b->objcore));

	if (b->spec != NULL)
		free(b->spec);
	FREE_OBJ(b);
}

/*--------------------------------------------------------------------
 * Get/release holds which prevent the ban_lurker from starting.
 * Holds are held while stevedores load zombie objects.
 */

void
BAN_Hold(void)
{

	Lck_Lock(&ban_mtx);
	/* Once holds are released, we allow no more */
	assert(ban_holds > 0);
	ban_holds++;
	Lck_Unlock(&ban_mtx);
}

void
BAN_Release(void)
{

	Lck_Lock(&ban_mtx);
	assert(ban_holds > 0);
	ban_holds--;
	Lck_Unlock(&ban_mtx);
	if (ban_holds == 0)
		WRK_BgThread(&ban_thread, "ban-lurker", ban_lurker, NULL);
}

/*--------------------------------------------------------------------
 * Extract time and length from ban-spec
 */

double
ban_time(const uint8_t *banspec)
{
	double t;

	assert(sizeof t == (BANS_LENGTH - BANS_TIMESTAMP));
	memcpy(&t, banspec, sizeof t);
	return (t);
}

unsigned
ban_len(const uint8_t *banspec)
{
	unsigned u;

	u = vbe32dec(banspec + BANS_LENGTH);
	return (u);
}

int
ban_equal(const uint8_t *bs1, const uint8_t *bs2)
{
	unsigned u;

	/*
	 * Compare two ban-strings.
	 */
	u = vbe32dec(bs1 + BANS_LENGTH);
	if (u != vbe32dec(bs2 + BANS_LENGTH))
		return (0);
	return (!memcmp(bs1 + BANS_LENGTH, bs2 + BANS_LENGTH, u - BANS_LENGTH));
}

void
ban_mark_completed(struct ban *b)
{
	unsigned ln;

	CHECK_OBJ_NOTNULL(b, BAN_MAGIC);
	Lck_AssertHeld(&ban_mtx);

	AN(b->spec);
	if (!(b->flags & BANS_FLAG_COMPLETED)) {
		ln = ban_len(b->spec);
		b->flags |= BANS_FLAG_COMPLETED;
		b->spec[BANS_FLAGS] |= BANS_FLAG_COMPLETED;
		VWMB();
		vbe32enc(b->spec + BANS_LENGTH, BANS_HEAD_LEN);
		VSC_C_main->bans_completed++;
		bans_persisted_fragmentation += ln - ban_len(b->spec);
		VSC_C_main->bans_persisted_fragmentation =
		    bans_persisted_fragmentation;
	}
}

/*--------------------------------------------------------------------
 * Access a lump of bytes in a ban test spec
 */

static const void *
ban_get_lump(const uint8_t **bs)
{
	const void *r;
	unsigned ln;

	while (**bs == 0xff)
		*bs += 1;
	ln = vbe32dec(*bs);
	*bs += PRNDUP(sizeof(uint32_t));
	assert(PAOK(*bs));
	r = (const void*)*bs;
	*bs += ln;
	return (r);
}

/*--------------------------------------------------------------------
 * Pick a test apart from a spec string
 */

static void
ban_iter(const uint8_t **bs, struct ban_test *bt)
{

	memset(bt, 0, sizeof *bt);
	bt->arg1 = *(*bs)++;
	if (bt->arg1 == BANS_ARG_REQHTTP || bt->arg1 == BANS_ARG_OBJHTTP) {
		bt->arg1_spec = (const char *)*bs;
		(*bs) += (*bs)[0] + 2;
	}
	bt->arg2 = ban_get_lump(bs);
	bt->oper = *(*bs)++;
	if (bt->oper == BANS_OPER_MATCH || bt->oper == BANS_OPER_NMATCH)
		bt->arg2_spec = ban_get_lump(bs);
}

/*--------------------------------------------------------------------
 * A new object is created, grab a reference to the newest ban
 */

void
BAN_NewObjCore(struct objcore *oc)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AZ(oc->ban);
	AN(oc->objhead);
	Lck_Lock(&ban_mtx);
	oc->ban = ban_start;
	ban_start->refcount++;
	VTAILQ_INSERT_TAIL(&ban_start->objcore, oc, ban_list);
	Lck_Unlock(&ban_mtx);
}

/*--------------------------------------------------------------------
 * An object is destroyed, release its ban reference
 */

void
BAN_DestroyObj(struct objcore *oc)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	Lck_Lock(&ban_mtx);
	CHECK_OBJ_ORNULL(oc->ban, BAN_MAGIC);
	if (oc->ban != NULL) {
		assert(oc->ban->refcount > 0);
		oc->ban->refcount--;
		VTAILQ_REMOVE(&oc->ban->objcore, oc, ban_list);
		oc->ban = NULL;
	}
	Lck_Unlock(&ban_mtx);
}

/*--------------------------------------------------------------------
 * Find a ban based on a timestamp.
 * Assume we have a BAN_Hold, so list traversal is safe.
 */

struct ban *
BAN_FindBan(double t0)
{
	struct ban *b;
	double t1;

	assert(ban_holds > 0);
	VTAILQ_FOREACH(b, &ban_head, list) {
		t1 = ban_time(b->spec);
		if (t1 == t0)
			return (b);
		if (t1 < t0)
			break;
	}
	return (NULL);
}

/*--------------------------------------------------------------------
 * Grab a reference to a ban and associate the objcore with that ban.
 * Assume we have a BAN_Hold, so list traversal is safe.
 */

void
BAN_RefBan(struct objcore *oc, struct ban *b)
{

	Lck_Lock(&ban_mtx);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AZ(oc->ban);
	CHECK_OBJ_NOTNULL(b, BAN_MAGIC);
	assert(ban_holds > 0);
	b->refcount++;
	VTAILQ_INSERT_TAIL(&b->objcore, oc, ban_list);
	oc->ban = b;
	Lck_Unlock(&ban_mtx);
}

/*--------------------------------------------------------------------
 * Compile a full ban list and export this area to the stevedores for
 * persistence.
 */

static void
ban_export(void)
{
	struct ban *b;
	struct vsb *vsb;
	unsigned ln;

	Lck_AssertHeld(&ban_mtx);
	ln = bans_persisted_bytes - bans_persisted_fragmentation;
	vsb = VSB_new_auto();
	AN(vsb);
	VTAILQ_FOREACH_REVERSE(b, &ban_head, banhead_s, list)
		AZ(VSB_bcat(vsb, b->spec, ban_len(b->spec)));
	AZ(VSB_finish(vsb));
	assert(VSB_len(vsb) == ln);
	STV_BanExport((const uint8_t *)VSB_data(vsb), VSB_len(vsb));
	VSB_destroy(&vsb);
	VSC_C_main->bans_persisted_bytes =
	    bans_persisted_bytes = ln;
	VSC_C_main->bans_persisted_fragmentation =
	    bans_persisted_fragmentation = 0;
}

/*
 * For both of these we do a full export on info failure to remove
 * holes in the exported list.
 * XXX: we should keep track of the size of holes in the last exported list
 * XXX: check if the ban_export should be batched in ban_cleantail
 */
void
ban_info_new(const uint8_t *ban, unsigned len)
{
	/* XXX martin pls review if ban_mtx needs to be held */
	Lck_AssertHeld(&ban_mtx);
	if (STV_BanInfoNew(ban, len))
		ban_export();
}

void
ban_info_drop(const uint8_t *ban, unsigned len)
{
	/* XXX martin pls review if ban_mtx needs to be held */
	Lck_AssertHeld(&ban_mtx);
	if (STV_BanInfoDrop(ban, len))
		ban_export();
}

/*--------------------------------------------------------------------
 * Put a skeleton ban in the list, unless there is an identical,
 * time & condition, ban already in place.
 *
 * If a newer ban has same condition, mark the inserted ban COMPLETED,
 * also mark any older bans, with the same condition COMPLETED.
 */

static void
ban_reload(const uint8_t *ban, unsigned len)
{
	struct ban *b, *b2;
	int duplicate = 0;
	double t0, t1, t2 = 9e99;

	ASSERT_CLI();
	Lck_AssertHeld(&ban_mtx);

	t0 = ban_time(ban);
	assert(len == ban_len(ban));

	VTAILQ_FOREACH(b, &ban_head, list) {
		t1 = ban_time(b->spec);
		assert(t1 < t2);
		t2 = t1;
		if (t1 == t0)
			return;
		if (t1 < t0)
			break;
		if (ban_equal(b->spec, ban))
			duplicate = 1;
	}

	VSC_C_main->bans++;
	VSC_C_main->bans_added++;

	b2 = ban_alloc();
	AN(b2);
	b2->spec = malloc(len);
	AN(b2->spec);
	memcpy(b2->spec, ban, len);
	if (ban[BANS_FLAGS] & BANS_FLAG_REQ) {
		VSC_C_main->bans_req++;
		b2->flags |= BANS_FLAG_REQ;
	}
	if (duplicate)
		VSC_C_main->bans_dups++;
	if (duplicate || (ban[BANS_FLAGS] & BANS_FLAG_COMPLETED))
		ban_mark_completed(b2);
	if (b == NULL)
		VTAILQ_INSERT_TAIL(&ban_head, b2, list);
	else
		VTAILQ_INSERT_BEFORE(b, b2, list);
	bans_persisted_bytes += len;
	VSC_C_main->bans_persisted_bytes = bans_persisted_bytes;

	/* Hunt down older duplicates */
	for (b = VTAILQ_NEXT(b2, list); b != NULL; b = VTAILQ_NEXT(b, list)) {
		if (b->flags & BANS_FLAG_COMPLETED)
			continue;
		if (ban_equal(b->spec, ban)) {
			ban_mark_completed(b);
			VSC_C_main->bans_dups++;
		}
	}
}

/*--------------------------------------------------------------------
 * Reload a series of persisted ban specs
 */

void
BAN_Reload(const uint8_t *ptr, unsigned len)
{
	const uint8_t *pe;
	unsigned l;

	AZ(ban_shutdown);
	pe = ptr + len;
	Lck_Lock(&ban_mtx);
	while (ptr < pe) {
		/* XXX: This can be optimized by traversing the live
		 * ban list together with the reload list (combining
		 * the loops in BAN_Reload and ban_reload). */
		l = ban_len(ptr);
		assert(ptr + l <= pe);
		ban_reload(ptr, l);
		ptr += l;
	}
	Lck_Unlock(&ban_mtx);
}

/*--------------------------------------------------------------------
 * Get a bans timestamp
 */

double
BAN_Time(const struct ban *b)
{

	if (b == NULL)
		return (0.0);

	CHECK_OBJ_NOTNULL(b, BAN_MAGIC);
	return (ban_time(b->spec));
}

/*--------------------------------------------------------------------
 * Evaluate ban-spec
 */

int
ban_evaluate(struct worker *wrk, const uint8_t *bs, struct objcore *oc,
    const struct http *reqhttp, unsigned *tests)
{
	struct ban_test bt;
	const uint8_t *be;
	const char *p;
	const char *arg1;

	be = bs + ban_len(bs);
	bs += BANS_HEAD_LEN;
	while (bs < be) {
		(*tests)++;
		ban_iter(&bs, &bt);
		arg1 = NULL;
		switch (bt.arg1) {
		case BANS_ARG_URL:
			AN(reqhttp);
			arg1 = reqhttp->hd[HTTP_HDR_URL].b;
			break;
		case BANS_ARG_REQHTTP:
			AN(reqhttp);
			(void)http_GetHdr(reqhttp, bt.arg1_spec, &p);
			arg1 = p;
			break;
		case BANS_ARG_OBJHTTP:
			arg1 = HTTP_GetHdrPack(wrk, oc, bt.arg1_spec);
			break;
		case BANS_ARG_OBJSTATUS:
			arg1 = HTTP_GetHdrPack(wrk, oc, H__Status);
			break;
		default:
			WRONG("Wrong BAN_ARG code");
		}

		switch (bt.oper) {
		case BANS_OPER_EQ:
			if (arg1 == NULL || strcmp(arg1, bt.arg2))
				return (0);
			break;
		case BANS_OPER_NEQ:
			if (arg1 != NULL && !strcmp(arg1, bt.arg2))
				return (0);
			break;
		case BANS_OPER_MATCH:
			if (arg1 == NULL ||
			    pcre_exec(bt.arg2_spec, NULL, arg1, strlen(arg1),
			    0, 0, NULL, 0) < 0)
				return (0);
			break;
		case BANS_OPER_NMATCH:
			if (arg1 != NULL &&
			    pcre_exec(bt.arg2_spec, NULL, arg1, strlen(arg1),
			    0, 0, NULL, 0) >= 0)
				return (0);
			break;
		default:
			WRONG("Wrong BAN_OPER code");
		}
	}
	return (1);
}

/*--------------------------------------------------------------------
 * Check an object against all applicable bans
 *
 * Return:
 *	-1 not all bans checked, but none of the checked matched
 *		Only if !has_req
 *	0 No bans matched, object moved to ban_start.
 *	1 Ban matched, object removed from ban list.
 */

int
BAN_CheckObject(struct worker *wrk, struct objcore *oc, struct req *req)
{
	struct ban *b;
	struct vsl_log *vsl;
	struct ban *b0, *bn;
	unsigned tests;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	Lck_AssertHeld(&oc->objhead->mtx);
	assert(oc->refcnt > 0);

	vsl = req->vsl;

	CHECK_OBJ_NOTNULL(oc->ban, BAN_MAGIC);

	/* First do an optimistic unlocked check */
	b0 = ban_start;
	CHECK_OBJ_NOTNULL(b0, BAN_MAGIC);

	if (b0 == oc->ban)
		return (0);

	/* If that fails, make a safe check */
	Lck_Lock(&ban_mtx);
	b0 = ban_start;
	bn = oc->ban;
	if (b0 != bn)
		bn->refcount++;
	Lck_Unlock(&ban_mtx);

	AN(bn);

	if (b0 == bn)
		return (0);


	/*
	 * This loop is safe without locks, because we know we hold
	 * a refcount on a ban somewhere in the list and we do not
	 * inspect the list past that ban.
	 */
	tests = 0;
	for (b = b0; b != bn; b = VTAILQ_NEXT(b, list)) {
		CHECK_OBJ_NOTNULL(b, BAN_MAGIC);
		if (b->flags & BANS_FLAG_COMPLETED)
			continue;
		if (ban_evaluate(wrk, b->spec, oc, req->http, &tests))
			break;
	}

	Lck_Lock(&ban_mtx);
	bn->refcount--;
	VSC_C_main->bans_tested++;
	VSC_C_main->bans_tests_tested += tests;

	if (b == bn) {
		/* not banned */
		oc->ban->refcount--;
		VTAILQ_REMOVE(&oc->ban->objcore, oc, ban_list);
		VTAILQ_INSERT_TAIL(&b0->objcore, oc, ban_list);
		b0->refcount++;
		oc->ban = b0;
		b = NULL;
	}

	if (VTAILQ_LAST(&ban_head, banhead_s)->refcount == 0)
		ban_kick_lurker();

	Lck_Unlock(&ban_mtx);

	if (b == NULL) {
		/* not banned */
		ObjSendEvent(wrk, oc, OEV_BANCHG);
		return (0);
	} else {
		VSLb(vsl, SLT_ExpBan, "%u banned lookup", ObjGetXID(wrk, oc));
		VSC_C_main->bans_obj_killed++;
		return (1);
	}
}

/*--------------------------------------------------------------------
 * CLI functions to add bans
 */

static void
ccf_ban(struct cli *cli, const char * const *av, void *priv)
{
	int narg, i;
	struct ban_proto *bp;
	const char *err = NULL;

	(void)priv;

	/* First do some cheap checks on the arguments */
	for (narg = 0; av[narg + 2] != NULL; narg++)
		continue;
	if ((narg % 4) != 3) {
		VCLI_Out(cli, "Wrong number of arguments");
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}
	for (i = 3; i < narg; i += 4) {
		if (strcmp(av[i + 2], "&&")) {
			VCLI_Out(cli, "Found \"%s\" expected &&", av[i + 2]);
			VCLI_SetResult(cli, CLIS_PARAM);
			return;
		}
	}

	bp = BAN_Build();
	if (bp == NULL) {
		VCLI_Out(cli, "Out of Memory");
		VCLI_SetResult(cli, CLIS_CANT);
		return;
	}
	for (i = 0; i < narg; i += 4) {
		err = BAN_AddTest(bp, av[i + 2], av[i + 3], av[i + 4]);
		if (err)
			break;
	}

	if (err == NULL)
		err = BAN_Commit(bp);

	if (err != NULL) {
		VCLI_Out(cli, "%s", err);
		BAN_Abandon(bp);
		VCLI_SetResult(cli, CLIS_PARAM);
	}
}

static void
ban_render(struct cli *cli, const uint8_t *bs)
{
	struct ban_test bt;
	const uint8_t *be;

	be = bs + ban_len(bs);
	bs += BANS_HEAD_LEN;
	while (bs < be) {
		ban_iter(&bs, &bt);
		switch (bt.arg1) {
		case BANS_ARG_URL:
			VCLI_Out(cli, "req.url");
			break;
		case BANS_ARG_REQHTTP:
			VCLI_Out(cli, "req.http.%.*s",
			    bt.arg1_spec[0] - 1, bt.arg1_spec + 1);
			break;
		case BANS_ARG_OBJHTTP:
			VCLI_Out(cli, "obj.http.%.*s",
			    bt.arg1_spec[0] - 1, bt.arg1_spec + 1);
			break;
		case BANS_ARG_OBJSTATUS:
			VCLI_Out(cli, "obj.status");
			break;
		default:
			WRONG("Wrong BANS_ARG");
		}
		switch (bt.oper) {
		case BANS_OPER_EQ:	VCLI_Out(cli, " == "); break;
		case BANS_OPER_NEQ:	VCLI_Out(cli, " != "); break;
		case BANS_OPER_MATCH:	VCLI_Out(cli, " ~ "); break;
		case BANS_OPER_NMATCH:	VCLI_Out(cli, " !~ "); break;
		default:
			WRONG("Wrong BANS_OPER");
		}
		VCLI_Out(cli, "%s", bt.arg2);
		if (bs < be)
			VCLI_Out(cli, " && ");
	}
}

static void
ccf_ban_list(struct cli *cli, const char * const *av, void *priv)
{
	struct ban *b, *bl;
	int64_t o;

	(void)av;
	(void)priv;

	/* Get a reference so we are safe to traverse the list */
	Lck_Lock(&ban_mtx);
	bl = VTAILQ_LAST(&ban_head, banhead_s);
	bl->refcount++;
	Lck_Unlock(&ban_mtx);

	VCLI_Out(cli, "Present bans:\n");
	VTAILQ_FOREACH(b, &ban_head, list) {
		o = bl == b ? 1 : 0;
		VCLI_Out(cli, "%10.6f %5ju %s", ban_time(b->spec),
		    (intmax_t)(b->refcount - o),
		    b->flags & BANS_FLAG_COMPLETED ? "C" : "-");
		if (DO_DEBUG(DBG_LURKER)) {
			VCLI_Out(cli, "%s%s %p ",
			    b->flags & BANS_FLAG_REQ ? "R" : "-",
			    b->flags & BANS_FLAG_OBJ ? "O" : "-",
			    b);
		}
		VCLI_Out(cli, "  ");
		ban_render(cli, b->spec);
		VCLI_Out(cli, "\n");
		if (VCLI_Overflow(cli))
			break;
		if (DO_DEBUG(DBG_LURKER)) {
			Lck_Lock(&ban_mtx);
			struct objcore *oc;
			VTAILQ_FOREACH(oc, &b->objcore, ban_list)
				VCLI_Out(cli, "  oc = %p\n", oc);
			Lck_Unlock(&ban_mtx);
		}
	}

	Lck_Lock(&ban_mtx);
	bl->refcount--;
	ban_kick_lurker();	// XXX: Mostly for testcase b00009.vtc
	Lck_Unlock(&ban_mtx);
}

static struct cli_proto ban_cmds[] = {
	{ CLICMD_BAN,				"", ccf_ban },
	{ CLICMD_BAN_LIST,			"", ccf_ban_list },
	{ NULL }
};

/*--------------------------------------------------------------------
 */

void
BAN_Compile(void)
{
	struct ban *b;

	/*
	 * All bans have been read from all persistent stevedores. Export
	 * the compiled list
	 */

	ASSERT_CLI();
	AZ(ban_shutdown);

	Lck_Lock(&ban_mtx);

	/* Report the place-holder ban */
	b = VTAILQ_FIRST(&ban_head);
	ban_info_new(b->spec, ban_len(b->spec));

	ban_export();

	Lck_Unlock(&ban_mtx);

	ban_start = VTAILQ_FIRST(&ban_head);
	BAN_Release();
}

void
BAN_Init(void)
{
	struct ban_proto *bp;

	Lck_New(&ban_mtx, lck_ban);
	CLI_AddFuncs(ban_cmds);

	ban_holds = 1;

	/* Add a placeholder ban */
	bp = BAN_Build();
	AN(bp);
	AZ(pthread_cond_init(&ban_lurker_cond, NULL));
	AZ(BAN_Commit(bp));
	Lck_Lock(&ban_mtx);
	ban_mark_completed(VTAILQ_FIRST(&ban_head));
	Lck_Unlock(&ban_mtx);
}

/*--------------------------------------------------------------------
 * Shutdown of the ban system.
 *
 * When this function returns, no new bans will be accepted, and no
 * bans will be dropped (ban lurker thread stopped), so that no
 * STV_BanInfo calls will be executed.
 */

void
BAN_Shutdown(void)
{
	void *status;

	Lck_Lock(&ban_mtx);
	ban_shutdown = 1;
	ban_kick_lurker();
	Lck_Unlock(&ban_mtx);

	AZ(pthread_join(ban_thread, &status));
	AZ(status);

	Lck_Lock(&ban_mtx);
	/* Export the ban list to compact it */
	ban_export();
	Lck_Unlock(&ban_mtx);
}
