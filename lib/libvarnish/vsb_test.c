
#ifdef VSB_TEST

#include <stdio.h>
#include <string.h>

#include "vdef.h"
#include "vas.h"
#include "vsb.h"

struct tc {
	int		how;
	int		inlen;
	const char	*in;
	const char	*out;
};

static struct tc tcs[] = {
	{
		VSB_QUOTE_HEX,
		5, "\x00\n\x7e\x7f\xff",
		"PFX0x000a7e7fff"
	},
	{
		VSB_QUOTE_HEX,
		5, "\0\0\0\0\0",
		"PFX0x0...0"
	},
	{
		VSB_QUOTE_HEX | VSB_QUOTE_NONL,
		5, "\x00\n\x7e\x7f\xff",
		"PFX0x000a7e7fff\n"
	},
	{
		VSB_QUOTE_ESCHEX,
		5, "\x00\n\x7e\x7f\xff",
		"PFX\\x00\\n~\\x7f\\xff",
	},
	{
		0,
		5, "\x00\n\x7e\x7f\xff",
		"PFX\\000\\n~\\177\\377",
	},
	{
		VSB_QUOTE_UNSAFE,
		5, "\x00\n\x7e\x7f\xff",
		"PFX\\000\nPFX~\\177\\377\n",
	},
	{
		VSB_QUOTE_UNSAFE,
		-1, "\n\"\\\t",
		"PFX\nPFX\"\\\\t\n"
	},
	{
		VSB_QUOTE_CSTR | VSB_QUOTE_ESCHEX,
		5, "\x00\n\x7e\x7f\xff",
		"PFX\"\\x00\\n\"\nPFX\"~\\x7f\\xff\"",
	},
	{
		VSB_QUOTE_JSON,
		5, "\x00\n\x7e\x7f\xff",
		"PFX\\u0000\\n~\x7f\xff",
	},
	{
		VSB_QUOTE_JSON | VSB_QUOTE_NONL,
		5, "\x00\n\x7e\x7f\xff",
		"PFX\\u0000\\n~\x7f\xff\n",
	},
	{
		VSB_QUOTE_CSTR,
		-1, "",
		"PFX\"\""
	},
	{
		VSB_QUOTE_CSTR,
		-1, "?",
		"PFX\"\\?\""
	},
	{
		VSB_QUOTE_NONL,
		-1, "\n\t",
		"PFX\nPFX\\t\n"
	},
	{
		0, -1, NULL, NULL
	}
};

int
main(int argc, char *argv[])
{
	int err = 0;
	struct tc *tc;
	struct vsb *vsb;
	struct vsb *vsbo;

	(void)argc;
	(void)argv;
	vsb = VSB_new_auto();
	AN(vsb);
	vsbo = VSB_new_auto();
	AN(vsbo);

	for (tc = tcs; tc->in; tc++) {
		VSB_quote_pfx(vsb, "PFX", tc->in, tc->inlen, tc->how);
		assert(VSB_finish(vsb) == 0);

		VSB_clear(vsbo);
		VSB_printf(vsbo, "0x%02x: ", tc->how);
		VSB_quote(vsbo, tc->in, tc->inlen, VSB_QUOTE_HEX);
		VSB_printf(vsbo, " -> ");
		VSB_quote(vsbo, VSB_data(vsb), -1, VSB_QUOTE_HEX);
		VSB_printf(vsbo, " (");
		VSB_quote(vsbo, tc->out, -1, VSB_QUOTE_ESCHEX);
		VSB_printf(vsbo, ")");
		if (strcmp(VSB_data(vsb), tc->out)) {
			VSB_printf(vsbo, "\nShould have been:\n\t");
			VSB_quote(vsbo, tc->out, -1, VSB_QUOTE_HEX);
			VSB_printf(vsbo, "\nThat's:\n\t");
			VSB_quote(vsbo, VSB_data(vsb), -1, VSB_QUOTE_ESCHEX);
			VSB_printf(vsbo, "\nvs:\n\t");
			VSB_quote(vsbo, tc->out, -1, VSB_QUOTE_ESCHEX);
			VSB_printf(vsbo, "\nFlags 0x%02x = ", tc->how);
			if (!tc->how)
				VSB_printf(vsbo, "\n\t0");
			if (tc->how & VSB_QUOTE_NONL)
				VSB_printf(vsbo, "\n\tVSB_QUOTE_NONL");
			if (tc->how & VSB_QUOTE_JSON)
				VSB_printf(vsbo, "\n\tVSB_QUOTE_JSON");
			if (tc->how & VSB_QUOTE_HEX)
				VSB_printf(vsbo, "\n\tVSB_QUOTE_HEX");
			if (tc->how & VSB_QUOTE_CSTR)
				VSB_printf(vsbo, "\n\tVSB_QUOTE_CSTR");
			if (tc->how & VSB_QUOTE_UNSAFE)
				VSB_printf(vsbo, "\n\tVSB_QUOTE_UNSAFE");
			if (tc->how & VSB_QUOTE_ESCHEX)
				VSB_printf(vsbo, "\n\tVSB_QUOTE_ESCHEX");
			VSB_printf(vsbo, "\n\n");
			err = 1;
		}
		AZ(VSB_finish(vsbo));
		printf("%s\n", VSB_data(vsbo));
		VSB_clear(vsb);
	}
	VSB_destroy(&vsb);
	VSB_destroy(&vsbo);
	printf("error is %i\n", err);
	return (err);
}

#endif /* VSB_TEST */
