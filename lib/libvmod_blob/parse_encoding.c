/*
 * for the time being, this code is auto-generated outside the varnishd source
 * tree, see
 * https://code.uplex.de/uplex-varnish/libvmod-blobcode/blob/master/src/gen_enum_parse.pl
 *
 * TODO: integrate in vmodtool.py or replace with something else
 * cf. the same TODO for the shard director in libvmod_directors
 * make generation of the offset-pointer p optional
 */

#include "parse_encoding.h"
#define term(c) ((c) == '\0')

enum encoding
parse_encoding (const char *m) {
	enum encoding r;

	switch (m[0]) {
	case 'B':	goto _0B;	// BASE64, BASE64URL, BASE64URLNOPAD
	case 'H':	goto _0H;	// HEX
	case 'I':	goto _0I;	// IDENTITY
	case 'U':	goto _0U;	// URL
	default:	goto invalid;
	}
	 _0B:
	switch (m[1]) {
	case 'A':	goto _1BA;	// BASE64, BASE64URL, BASE64URLNOPAD
	default:	goto invalid;
	}
	 _1BA:
	switch (m[2]) {
	case 'S':	goto _2BAS;	// BASE64, BASE64URL, BASE64URLNOPAD
	default:	goto invalid;
	}
	 _2BAS:
	switch (m[3]) {
	case 'E':	goto _3BASE;	// BASE64, BASE64URL, BASE64URLNOPAD
	default:	goto invalid;
	}
	 _3BASE:
	switch (m[4]) {
	case '6':	goto _4BASE6;	// BASE64, BASE64URL, BASE64URLNOPAD
	default:	goto invalid;
	}
	 _4BASE6:
	switch (m[5]) {
	case '4':	goto _5BASE64;	// BASE64, BASE64URL, BASE64URLNOPAD
	default:	goto invalid;
	}
	 _5BASE64:
	//BASE64
	if (term(m[6])) {
	    r = BASE64;
	    goto ok;
	}
	switch (m[6]) {
	case 'U':	goto _6BASE64U;	// BASE64URL, BASE64URLNOPAD
	default:	goto invalid;
	}
	 _6BASE64U:
	switch (m[7]) {
	case 'R':	goto _7BASE64UR;	// BASE64URL, BASE64URLNOPAD
	default:	goto invalid;
	}
	 _7BASE64UR:
	switch (m[8]) {
	case 'L':	goto _8BASE64URL;	// BASE64URL, BASE64URLNOPAD
	default:	goto invalid;
	}
	 _8BASE64URL:
	//BASE64URL
	if (term(m[9])) {
	    r = BASE64URL;
	    goto ok;
	}
	switch (m[9]) {
	case 'N':	goto _9BASE64URLN;	// BASE64URLNOPAD
	default:	goto invalid;
	}
	 _9BASE64URLN:
	//BASE64URLNOPAD
	if ((m[10] == 'O') && (m[11] == 'P') && (m[12] == 'A') && (m[13] == 'D') && (term(m[14]))) {
	    r = BASE64URLNOPAD;
	    goto ok;
	}
	goto invalid;
	 _0H:
	//HEX
	if ((m[1] == 'E') && (m[2] == 'X') && (term(m[3]))) {
	    r = HEX;
	    goto ok;
	}
	goto invalid;
	 _0I:
	//IDENTITY
	if ((m[1] == 'D') && (m[2] == 'E') && (m[3] == 'N') && (m[4] == 'T') && (m[5] == 'I') && (m[6] == 'T') && (m[7] == 'Y') && (term(m[8]))) {
	    r = IDENTITY;
	    goto ok;
	}
	goto invalid;
	 _0U:
	//URL
	if ((m[1] == 'R') && (m[2] == 'L') && (term(m[3]))) {
	    r = URL;
	    goto ok;
	}
	goto invalid;
  ok:
	return r;
  invalid:
    return _INVALID;
}
