/*
 * $Id$
 *
 * Ban processing
 */

#include <stdlib.h>
#include <string.h>
#include <regex.h>

#include "shmlog.h"
#include "cli_priv.h"
#include "cache.h"

struct ban {
	TAILQ_ENTRY(ban)	list;
	unsigned		gen;
	regex_t			regexp;
	char			*ban;
};

static TAILQ_HEAD(,ban) ban_head = TAILQ_HEAD_INITIALIZER(ban_head);
static unsigned ban_next;
static struct ban *ban_start;

static void
AddBan(const char *regexp)
{
	struct ban *b;
	int i;

	b = calloc(sizeof *b, 1);
	assert(b != NULL);

	i = regcomp(&b->regexp, regexp, REG_EXTENDED | REG_NOSUB);
	if (i) {
		char buf[512];
	
		regerror(i, &b->regexp, buf, sizeof buf);
		VSL(SLT_Debug, 0, "REGEX: <%s>", buf);
	}
	b->gen = ++ban_next;
	b->ban = strdup(regexp);
	TAILQ_INSERT_HEAD(&ban_head, b, list);
	ban_start = b;
}

void
BAN_NewObj(struct object *o)
{

	o->ban_seq = ban_next;
}

int
BAN_CheckObject(struct object *o, const char *url)
{
	struct ban *b, *b0;
	int i;

	b0 = ban_start;
	for (b = b0;
	    b != NULL && b->gen > o->ban_seq;
	    b = TAILQ_NEXT(b, list)) {
		i = regexec(&b->regexp, url, 0, NULL, 0);
		if (!i)
			return (1);
	} 
	o->ban_seq = b0->gen;
	return (0);
}

void
cli_func_url_purge(struct cli *cli, char **av, void *priv)
{

	AddBan(av[2]);
	cli_out(cli, "PURGE %s\n", av[2]);
}

void
BAN_Init(void)
{

	AddBan("a");
}
