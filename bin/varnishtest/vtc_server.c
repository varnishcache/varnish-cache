
#include <stdio.h>

#include "vtc.h"

void
cmd_server(char **av, void *priv)
{

	printf("cmd_server(%p)\n", priv);
	while (*av)
		printf("\t<%s>\n", *av++);
}
