
#include <stdio.h>

#define P(x, ...) printf(x "\n", ##__VA_ARGS__)
#define VSC_F(n, t, l, f, e, d) printf("%s – %s\n\t%s\n\n", #n, e, d);

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

	P("MAIN COUNTERS");
	P("=============");
	P("");
#define VSC_DO_MAIN
#include "vsc_fields.h"
#undef VSC_DO_MAIN

	P("");
	P("LOCK COUNTERS");
	P("=============");
	P("");
#define VSC_DO_LCK
#include "vsc_fields.h"
#undef VSC_DO_LCK

	P("");
	P("PER MALLOC STORAGE COUNTERS");
	P("===========================");
	P("");
#define VSC_DO_SMA
#include "vsc_fields.h"
#undef  VSC_DO_SMA

	P("");
	P("PER FILE STORAGE COUNTERS");
	P("=========================");
	P("");
#define VSC_DO_SMF
#include "vsc_fields.h"
#undef VSC_DO_SMF

	P("");
	P("PER BACKEND COUNTERS");
	P("====================");
	P("");
#define VSC_DO_VBE
#include "vsc_fields.h"
#undef VSC_DO_VBE

	return 0;
}

