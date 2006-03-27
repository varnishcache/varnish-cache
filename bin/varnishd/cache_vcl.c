/*
 * $Id$
 */

#include <stdio.h>
#include <dlfcn.h>

#include "vcl_lang.h"
#include "cache.h"

int
CVCL_Load(const char *fn, const char *name)
{
	void *dlh;
	struct VCL_conf *vc;

	dlh = dlopen(fn, RTLD_NOW | RTLD_LOCAL);
	if (dlh == NULL) {
		fprintf(stderr, "dlopen(%s): %s\n", fn, dlerror());
		return (1);
	}
	vc = dlsym(dlh, "VCL_conf");
	if (vc == NULL) {
		fprintf(stderr, "No VCL_conf symbol\n");
		return (1);
	}
	if (vc->magic != VCL_CONF_MAGIC) {
		fprintf(stderr, "Wrong VCL_CONF_MAGIC\n");
		return (1);
	}
	fprintf(stderr, "Loaded \"%s\" as \"%s\"\n", fn , name);
	return (0);
}
