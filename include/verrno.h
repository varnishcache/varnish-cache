/*-
 * Written by Nils Goroll based upon a draft by Poul-Henning Kamp
 *
 * This file is in the public domain.
 *
 * trivial strerror() wrapper never returning NULL
 */

#include <errno.h>

const char * vstrerror(int e);
