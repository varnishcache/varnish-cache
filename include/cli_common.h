/*
 * $Id$
 */

struct cli {
	struct vsb		*sb;
	enum cli_status_e	result;
};

int cli_writeres(int fd, struct cli *cli);
int cli_readres(int fd, unsigned *status, char **ptr, double tmo);
extern struct cli_proto CLI_cmds[];

cli_func_t cli_func_ping;
