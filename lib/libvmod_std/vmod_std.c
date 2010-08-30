#include "vmod.h"

const char *
vmod_toupper(const char *s, ...)
{

	(void)s;
	return ("UPPER");
}

const char *
vmod_tolower(const char *s, ...)
{

	(void)s;
	return ("LOWER");
}

double
vmod_real(const char *s, double d)
{

	(void)s;
	(void)d;
	return (3.1415);
}
