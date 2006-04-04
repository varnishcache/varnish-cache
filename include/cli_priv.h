/*
 * $Id$
 *
 * Varnish process internal CLI stuff.
 *
 * XXX: at a latter date we may want to move some to cli.h/libvarnishapi
 *
 */

#define CLI_PRIV_H

struct cli;	/* NB: struct cli is opaque at this level.  */

typedef void cli_func_t(struct cli*, const char **av, void *priv);

struct cli_proto {
	/* These must match the CLI_* macros in cli.h */
	const char		*request;
	const char		*syntax;
	const char		*help;
	unsigned		minarg;
	unsigned		maxarg;

	/* Dispatch information */
	cli_func_t		*func;
	void			*priv;
};

/* The implementation must provide these functions */
void cli_out(struct cli *cli, const char *fmt, ...);
void cli_param(struct cli *cli);
void cli_result(struct cli *cli, unsigned r);

/* From libvarnish/cli.c */
void cli_dispatch(struct cli *cli, struct cli_proto *clp, const char *line);
cli_func_t	cli_func_help;
