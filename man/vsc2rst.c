/* XXX: Copyright ?? */
#include "config.h"

#include <stdio.h>

#define VSC_LEVEL_F(v,l,e,d)		\
	static const char VSC_level_##v[] = l;
#include "tbl/vsc_levels.h"
#undef VSC_LEVEL_F

#define P(x, ...)			\
	printf(x "\n", ##__VA_ARGS__)
#define VSC_LEVEL_F(v,l,e,d)		\
	printf("%s – %s\n\t%s\n\n", l, e, d);
#define VSC_F(n, t, l, f, v, e, d)	\
	printf("%s – %s (%s)\n\t%s\n\n", #n, e, VSC_level_##v, d);

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	P("================");
	P("varnish-counters");
	P("================");
	P("");

	P("---------------------------------");
	P("Varnish counter field definitions");
	P("---------------------------------");

	P(":Author: Tollef Fog Heen");
	P(":Date:   2011-09-20");
	P(":Version: 1.0");
	P(":Manual section: 7");
	P("");

	P("COUNTER LEVELS");
	P("==============");
	P("");
#include "tbl/vsc_levels.h"

	P("");
	P("MAIN COUNTERS");
	P("=============");
	P("");
#include "tbl/vsc_f_main.h"

	P("");
	P("LOCK COUNTERS");
	P("=============");
	P("");
#define VSC_DO_LCK
#include "tbl/vsc_fields.h"
#undef VSC_DO_LCK

	P("");
	P("PER MALLOC STORAGE COUNTERS");
	P("===========================");
	P("");
#define VSC_DO_SMA
#include "tbl/vsc_fields.h"
#undef  VSC_DO_SMA

	P("");
	P("PER FILE STORAGE COUNTERS");
	P("=========================");
	P("");
#define VSC_DO_SMF
#include "tbl/vsc_fields.h"
#undef VSC_DO_SMF

	P("");
	P("PER BACKEND COUNTERS");
	P("====================");
	P("");
#define VSC_DO_VBE
#include "tbl/vsc_fields.h"
#undef VSC_DO_VBE

	return (0);
}

