
#ifdef VSB_TEST

#include <stdio.h>
#include <string.h>

#include "vdef.h"
#include "vas.h"
#include "vsb.h"

struct tc {
	int		how;
	const char	*in;
	const char	*out;
};

static struct tc tcs[] = {
	{
	    VSB_QUOTE_GLOB,
	    "abcdefghijklmnopqrstvwxyz",
	    "abcdefghijklmnopqrstvwxyz"
	}, {
	    VSB_QUOTE_GLOB,
	    "ABCDEFGHIJKLMNOPQRSTVWXYZ",
	    "ABCDEFGHIJKLMNOPQRSTVWXYZ"
	}, {
	    VSB_QUOTE_GLOB,
	    "01234567789",
	    "01234567789"
	}, {
	    VSB_QUOTE_GLOB,
	    "abcde[f-g]{h,i,j}\\l?*xyz",
	    "abcde\\[f-g\\]\\{h,i,j\\}\\\\l\\?\\*xyz"
	}, {
	    VSB_QUOTE_GLOB,
	    "0123\t \"\r\v\n'",
	    "0123\\\\t \\\"\\\\r\\\\v\\\\n'"
	}, {
	    0, NULL, NULL
	}
};

int
main(int argc, char *argv[])
{
	int err = 0;
	struct tc *tc;
	struct vsb *vsb;

	(void)argc;
	(void)argv;
	vsb = VSB_new_auto();
	AN(vsb);

	for (tc = tcs; tc->in; tc++) {
		VSB_quote(vsb, tc->in, -1, tc->how);
		assert(VSB_finish(vsb) == 0);

		printf("%s -> %s", tc->in, VSB_data(vsb));
		if (strcmp(VSB_data(vsb), tc->out)) {
			printf(", but should have been %s",  tc->out);
			err = 1;
		}
		printf("\n");
		VSB_clear(vsb);
	}
	VSB_delete(vsb);
	printf("error is %i\n", err);
	return (err);
}

#endif /* VSB_TEST */
