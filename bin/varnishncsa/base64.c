/*
 * Written by Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * This file is in the public domain.
 */

#include "config.h"

#include <sys/types.h>
#include "varnishapi.h"

static const char b64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char i64[256];

void
VB64_init(void)
{
	int i;
	const char *p;

	for (i = 0; i < 256; i++)
		i64[i] = -1;
	for (p = b64, i = 0; *p; p++, i++)
		i64[(int)*p] = (char)i;
	i64['='] = 0;
}

int
VB64_decode(char *d, unsigned dlen, const char *s)
{
	unsigned u, v, l;
	int i;

	u = 0;
	l = 0;
	while (*s) {
		for (v = 0; v < 4; v++) {
			if (!*s)
				break;
			i = i64[(int)*s++];
			if (i < 0)
				return (-1);
			u <<= 6;
			u |= i;
		}
		for (v = 0; v < 3; v++) {
			if (l >= dlen - 1)
				return (-1);
			*d = (u >> 16) & 0xff;
			u <<= 8;
			l++;
			d++;
		}
	}
	*d = '\0';
	return (0);
}

#ifdef TEST_DRIVER
#include <stdio.h>

const char *test1 =
"TWFuIGlzIGRpc3Rpbmd1aXNoZWQsIG5vdCBvbmx5IGJ5IGhpcyByZWFzb24sIGJ1dCBieSB0aGlz"
"IHNpbmd1bGFyIHBhc3Npb24gZnJvbSBvdGhlciBhbmltYWxzLCB3aGljaCBpcyBhIGx1c3Qgb2Yg"
"dGhlIG1pbmQsIHRoYXQgYnkgYSBwZXJzZXZlcmFuY2Ugb2YgZGVsaWdodCBpbiB0aGUgY29udGlu"
"dWVkIGFuZCBpbmRlZmF0aWdhYmxlIGdlbmVyYXRpb24gb2Yga25vd2xlZGdlLCBleGNlZWRzIHRo"
"ZSBzaG9ydCB2ZWhlbWVuY2Ugb2YgYW55IGNhcm5hbCBwbGVhc3VyZS4=";

int
main(int argc, char **argv)
{
	int i;
	char buf[BUFSIZ];
	unsigned l;

	(void)argc;
	(void)argv;

	VB64_init();
	l = sizeof buf;
	VB64_decode(buf, &l, test1);
	printf("%s\n", buf);
	return (0);
}
#endif
