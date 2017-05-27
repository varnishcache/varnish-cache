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
 * Ban processing
 *
 * A ban consists of a number of conditions (or tests), all of which must be
 * satisfied.  Here are some potential bans we could support:
 *
 *	req.url == "/foo"
 *	req.url ~ ".iso" && obj.size > 10MB
 *	req.http.host ~ "web1.com" && obj.http.set-cookie ~ "USER=29293"
 *
 * We make the "&&" mandatory from the start, leaving the syntax space
 * for latter handling of "||" as well.
 *
 * Bans are compiled into bytestrings as follows:
 *	8 bytes	- double: timestamp		XXX: Byteorder ?
 *	4 bytes - be32: length
 *	1 byte - flags: 0x01: BAN_F_{REQ|OBJ|COMPLETED}
 *	N tests
 * A test have this form:
 *	1 byte - arg (see ban_vars.h col 3 "BANS_ARG_XXX")
 *	(n bytes) - http header name, canonical encoding
 *	lump - comparison arg
 *	1 byte - operation (BANS_OPER_)
 *	(lump) - compiled regexp
 * A lump is:
 *	4 bytes - be32: length
 *	n bytes - content
 *
 * In a perfect world, we should vector through VRE to get to PCRE,
 * but since we rely on PCRE's ability to encode the regexp into a
 * byte string, that would be a little bit artificial, so this is
 * the exception that confirms the rule.
 *
 */

/*--------------------------------------------------------------------
 * BAN string defines & magic markers
 */

#define BANS_TIMESTAMP		0
#define BANS_LENGTH		8
#define BANS_FLAGS		12
#define BANS_HEAD_LEN		16

#define BANS_FLAG_REQ		(1<<0)
#define BANS_FLAG_OBJ		(1<<1)
#define BANS_FLAG_COMPLETED	(1<<2)
#define BANS_FLAG_HTTP		(1<<3)

#define BANS_OPER_EQ		0x10
#define BANS_OPER_NEQ		0x11
#define BANS_OPER_MATCH		0x12
#define BANS_OPER_NMATCH	0x13

#define BANS_ARG_URL		0x18
#define BANS_ARG_REQHTTP	0x19
#define BANS_ARG_OBJHTTP	0x1a
#define BANS_ARG_OBJSTATUS	0x1b

/*--------------------------------------------------------------------*/

struct ban {
	unsigned		magic;
#define BAN_MAGIC		0x700b08ea
	unsigned		flags;		/* BANS_FLAG_* */
	VTAILQ_ENTRY(ban)	list;
	VTAILQ_ENTRY(ban)	l_list;
	int64_t			refcount;

	VTAILQ_HEAD(,objcore)	objcore;
	uint8_t			*spec;
};

VTAILQ_HEAD(banhead_s,ban);

bgthread_t ban_lurker;
extern struct lock ban_mtx;
extern int ban_shutdown;
extern struct banhead_s ban_head;
extern struct ban * volatile ban_start;
extern pthread_cond_t	ban_lurker_cond;
extern uint64_t bans_persisted_bytes;
extern uint64_t bans_persisted_fragmentation;

void ban_mark_completed(struct ban *b);
unsigned ban_len(const uint8_t *banspec);
void ban_info_new(const uint8_t *ban, unsigned len);
void ban_info_drop(const uint8_t *ban, unsigned len);

int ban_evaluate(struct worker *wrk, const uint8_t *bs, struct objcore *oc,
    const struct http *reqhttp, unsigned *tests);
double ban_time(const uint8_t *banspec);
int ban_equal(const uint8_t *bs1, const uint8_t *bs2);
void BAN_Free(struct ban *b);
void ban_kick_lurker(void);
