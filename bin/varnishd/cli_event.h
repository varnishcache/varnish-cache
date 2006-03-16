/*
 * $Id$
 */

struct cli {
	struct bufferevent	*bev0, *bev1;
	struct sbuf		*sb;
	unsigned		verbose;
	enum cli_status_e	result;
	struct cli_proto	*cli_proto;
};

struct cli *cli_setup(int fdr, int fdw, int ver, struct cli_proto *cli_proto);

