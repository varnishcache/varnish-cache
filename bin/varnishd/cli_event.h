/*
 * $Id$
 */

struct cli {
	struct bufferevent	*bev0, *bev1;
	struct sbuf		*sb;
	unsigned		verbose;
	unsigned		suspend;
	enum cli_status_e	result;
	struct cli_proto	*cli_proto;
};

struct cli *cli_setup(struct event_base *eb, int fdr, int fdw, int ver, struct cli_proto *cli_proto);
void cli_suspend(struct cli *cli);
void cli_resume(struct cli *cli);
void cli_encode_string(struct evbuffer *buf, char *b);
extern struct cli_proto CLI_cmds[];
