/*-
 * Written by Nils Goroll based upon a draft by Poul-Henning Kamp
 *
 * This file is in the public domain.
 *
 * trivial strerror() wrapper never returning NULL
 */

#include "config.h"

#include <string.h>

#include "verrno.h"

const char *
vstrerror(int e)
{
	const char *p;
	int oerrno = errno;

	p = strerror(e);
	if (p != NULL)
		return (p);

	errno = oerrno;
	return ("strerror(3) returned NULL");
}
