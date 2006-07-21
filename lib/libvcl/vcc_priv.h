/*
 * Stuff shared between main.c and fixed_token.c
 */

#include "vcc_token_defs.h"

#define isident1(c) (isalpha(c))
#define isident(c) (isalpha(c) || isdigit(c) || (c) == '_')
#define isvar(c) (isident(c) || (c) == '.')
unsigned vcl_fixed_token(const char *p, const char **q);
extern const char *vcl_tnames[256];
void vcl_init_tnames(void);
void vcl_output_lang_h(FILE *f);
