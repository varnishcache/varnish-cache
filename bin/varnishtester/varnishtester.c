
#include <stdlib.h>

#include "libvarnish.h"


/*--------------------------------------------------------------------
 * stubs to survice linkage
 */
 
#include "cli_priv.h"

struct cli;

void cli_out(struct cli *cli, const char *fmt, ...) { (void)cli; (void)fmt; abort(); }
void cli_param(struct cli *cli) { (void)cli; abort(); }
void cli_result(struct cli *cli, unsigned res) { (void)cli; (void)res; abort(); }

/*--------------------------------------------------------------------*/

int
main(int argc, char **argv)
{

	(void)argc;
	(void)argv;

	return (0);
}
