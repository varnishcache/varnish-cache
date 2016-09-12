/*
 * for the time being, this code is auto-generated outside the varnishd source
 * tree, see
 * https://code.uplex.de/uplex-varnish/libvmod-vslp/blob/shard/src/gen_enum_parse.pl
 *
 * TODO: integrate in vmodtool.py or replace with something else
 */

#include "shard_parse_vcc_enums.h"
#define term(c) ((c) == '\0')



enum alg_e parse_alg_e (const char *m) {
	enum alg_e r;

	switch (m[0]) {
	case 'C':	goto _0C;	// CRC32
	case 'R':	goto _0R;	// RS
	case 'S':	goto _0S;	// SHA256
	default:	goto invalid;
	}
	 _0C:
	//CRC32
	if ((m[1] == 'R') && (m[2] == 'C') && (m[3] == '3') && (m[4] == '2') && (term(m[5]))) {
	    r = CRC32;
	    goto ok;
	}
	goto invalid;
	 _0R:
	//RS
	if ((m[1] == 'S') && (term(m[2]))) {
	    r = RS;
	    goto ok;
	}
	goto invalid;
	 _0S:
	//SHA256
	if ((m[1] == 'H') && (m[2] == 'A') && (m[3] == '2') && (m[4] == '5') && (m[5] == '6') && (term(m[6]))) {
	    r = SHA256;
	    goto ok;
	}
	goto invalid;
  ok:
	return r;
  invalid:
    return _ALG_E_INVALID;
}


enum by_e parse_by_e (const char *m) {
	enum by_e r;

	switch (m[0]) {
	case 'B':	goto _0B;	// BLOB
	case 'H':	goto _0H;	// HASH
	case 'K':	goto _0K;	// KEY
	case 'U':	goto _0U;	// URL
	default:	goto invalid;
	}
	 _0B:
	//BLOB
	if ((m[1] == 'L') && (m[2] == 'O') && (m[3] == 'B') && (term(m[4]))) {
	    r = BY_BLOB;
	    goto ok;
	}
	goto invalid;
	 _0H:
	//HASH
	if ((m[1] == 'A') && (m[2] == 'S') && (m[3] == 'H') && (term(m[4]))) {
	    r = BY_HASH;
	    goto ok;
	}
	goto invalid;
	 _0K:
	//KEY
	if ((m[1] == 'E') && (m[2] == 'Y') && (term(m[3]))) {
	    r = BY_KEY;
	    goto ok;
	}
	goto invalid;
	 _0U:
	//URL
	if ((m[1] == 'R') && (m[2] == 'L') && (term(m[3]))) {
	    r = BY_URL;
	    goto ok;
	}
	goto invalid;
  ok:
	return r;
  invalid:
    return _BY_E_INVALID;
}


enum healthy_e parse_healthy_e (const char *m) {
	int p;
	enum healthy_e r;

	switch (m[0]) {
	case 'A':	goto _0A;	// ALL
	case 'C':	goto _0C;	// CHOSEN
	case 'I':	goto _0I;	// IGNORE
	default:	goto invalid;
	}
	 _0A:
	//ALL
	if ((m[1] == 'L') && (m[2] == 'L') && (term(m[3]))) {
	    r = ALL;
	    p = 3;
	    goto ok;
	}
	goto invalid;
	 _0C:
	//CHOSEN
	if ((m[1] == 'H') && (m[2] == 'O') && (m[3] == 'S') && (m[4] == 'E') && (m[5] == 'N') && (term(m[6]))) {
	    r = CHOSEN;
	    p = 6;
	    goto ok;
	}
	goto invalid;
	 _0I:
	//IGNORE
	if ((m[1] == 'G') && (m[2] == 'N') && (m[3] == 'O') && (m[4] == 'R') && (m[5] == 'E') && (term(m[6]))) {
	    r = IGNORE;
	    p = 6;
	    goto ok;
	}
	goto invalid;
  ok:
	return r;
  invalid:
    return _HEALTHY_E_INVALID;
    (void)p;
}
