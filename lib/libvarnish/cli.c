/*
 * $Id$
 *
 * Stuff for handling the CLI protocol
 */

#include <ctype.h>
#include <stdio.h>

#include <cli.h>
#include <cli_priv.h>
#include <libvarnish.h>

/*
 * Generic help function.
 *
 * priv must point to cli_proto array
 */

void
cli_func_help(struct cli *cli, char **av, void *priv)
{
	unsigned u;
	struct cli_proto *cp;

	if (av[2] == NULL) {
		cli_out(cli, "Available commands:\n");
		for (cp = priv; cp->request != NULL; cp++)
			cli_out(cli, "%s\n", cp->syntax);
		return;
	}
	for (cp = priv; cp->request != NULL; cp++) {
		if (!strcmp(cp->request, av[2])) {
			cli_out(cli, "%s\n%s\n", cp->syntax, cp->help);
			return;
		}
	}
	cli_param(cli);
}

void
cli_dispatch(struct cli *cli, struct cli_proto *clp, const char *line)
{
	char **av;
	unsigned u;
	struct cli_proto *cp;

	cli_result(cli, CLIS_OK);
	/* XXX: syslog commands */
	av = ParseArgv(line, 0);
	do {
		if (av[0] != NULL) {
			cli_out(cli, "Syntax Error: %s\n", av[0]);
			cli_result(cli, CLIS_SYNTAX);
			break;
		}
		if (av[1] == NULL)
			break;
		if (isupper(av[1][0])) {
			cli_out(cli,
			    "all commands are in lower-case.\n");
			cli_result(cli, CLIS_UNKNOWN);
			break;
		}
		for (cp = clp; cp->request != NULL; cp++)
			if (!strcmp(av[1], cp->request))
				break;
		if (cp->request == NULL) {
			cli_out(cli,
			    "Unknown request, type 'help' for more info.\n");
			cli_result(cli, CLIS_UNKNOWN);
			break;
		}

		if (cp->func == NULL) {
			cli_out(cli, "Unimplemented\n");
			cli_result(cli, CLIS_UNIMPL);
			break;
		}

		for (u = 0; u <= cp->minarg; u++) {
			if (av[u + 1] != NULL)
				continue;
			cli_out(cli, "Too few parameters\n");
			cli_result(cli, CLIS_TOOFEW);
			break;
		}
		if (u <= cp->minarg)
			break;
		for (; u <= cp->maxarg; u++)
			if (av[u + 1] == NULL)
				break;
		if (av[u + 1] != NULL) {
			cli_out(cli, "Too many parameters\n");
			cli_result(cli, CLIS_TOOMANY);
			break;
		}

		cp->func(cli, av, cp->priv);

	} while (0);
	FreeArgv(av);
}
