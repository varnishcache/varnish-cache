/*
 * Stuff shared between main.c and fixed_token.c
 */

#include "vcl_token_defs.h"
#include <ctype.h>

#define isident1(c) (isalpha(c))
#define isident(c) (isalpha(c) || isdigit(c) || (c) == '_')
#define isvar(c) (isident(c) || (c) == '.')
unsigned fixed_token(const char *p, const char **q);
extern const char *tnames[256];
