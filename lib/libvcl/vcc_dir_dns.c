/*-
 * Copyright (c) 2009 Redpill Linpro AS
 * Copyright (c) 2010 Varnish Software AS
 * All rights reserved.
 *
 * Author: Kristian Lyngstol <kristian@bohemians.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <sys/types.h>
#include <sys/socket.h>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>

#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"
#include "libvarnish.h"

/*--------------------------------------------------------------------
 * Parse directors
 */


static struct tokenlist_dir_backend_defaults {
	char *port;
	char *hostheader;
	double connect_timeout;
	double first_byte_timeout;
	double between_bytes_timeout;
	unsigned max_connections;
	unsigned saint;
} b_defaults;

static void
vcc_dir_initialize_defaults(void)
{
	b_defaults.port = NULL;
	b_defaults.hostheader = NULL;
	b_defaults.connect_timeout = -1.0;
	b_defaults.first_byte_timeout = -1.0;
	b_defaults.between_bytes_timeout = -1.0;
	b_defaults.max_connections = UINT_MAX;
	b_defaults.saint = UINT_MAX;
}

static const struct token *dns_first;
static void
print_backend(struct tokenlist *tl,
	      int serial,
	      const uint8_t *ip)
{
	char vgcname[BUFSIZ];
	char strip[16];
	struct token tmptok;
	struct vsb *vsb;

	bprintf(strip, "%u.%u.%u.%u", ip[3], ip[2], ip[1], ip[0]);
	tmptok.dec = strip;
	bprintf(vgcname, "%.*s_%d", PF(tl->t_dir), serial);
	vsb = vsb_newauto();
	AN(vsb);
	tl->fb = vsb;
	Fc(tl, 0, "\t{ .host = VGC_backend_%s },\n",vgcname);
	Fh(tl, 1, "\n#define VGC_backend_%s %d\n", vgcname, serial);

	Fb(tl, 0, "\nstatic const struct vrt_backend vgc_dir_priv_%s = {\n",
	    vgcname);

	Fb(tl, 0, "\t.vcl_name = \"%.*s", PF(tl->t_dir));
	if (serial >= 0)
		Fb(tl, 0, "[%d]", serial);
	Fb(tl, 0, "\",\n");
	Emit_Sockaddr(tl, &tmptok, b_defaults.port);
	vcc_EmitBeIdent(tl, tl->fb, serial, dns_first , tl->t);

	Fb(tl, 0, "\t.hosthdr = \"");
	if (b_defaults.hostheader != NULL)
		Fb(tl,0, b_defaults.hostheader);
	else
		Fb(tl,0, strip);
	Fb(tl, 0, "\",\n");

	Fb(tl, 0, "\t.saintmode_threshold = %d,\n",b_defaults.saint);
#define FB_TIMEOUT(type) do { \
		if (b_defaults.type != -1.0) \
			Fb(tl, 0, "\t.%s = %g,\n",#type,b_defaults.type); \
		} while (0)
	FB_TIMEOUT(connect_timeout);
	FB_TIMEOUT(first_byte_timeout);
	FB_TIMEOUT(between_bytes_timeout);

	Fb(tl, 0, "};\n");
	tl->fb = NULL;
	vsb_finish(vsb);
	Fh(tl, 0, "%s", vsb_data(vsb));
	vsb_delete(vsb);
	Fi(tl, 0, "\tVRT_init_dir(cli, VCL_conf.director, \"simple\",\n"
	    "\t    VGC_backend_%s, &vgc_dir_priv_%s);\n", vgcname, vgcname);
	Ff(tl, 0, "\tVRT_fini_dir(cli, VGCDIR(%s));\n", vgcname);
	tl->ndirector++;
}

/*
 * Output backends for all IPs in the range supplied by
 * "a[0].a[1].a[2].a[3]/inmask".
 *
 * XXX:
 * This assumes that a uint32_t can be safely accessed as an array of 4
 * uint8_ts.
 */
static void
vcc_dir_dns_makebackend(struct tokenlist *tl, 
			int *serial,
			const unsigned char a[],
			int inmask)
{
	uint32_t ip4=0;
	uint32_t ip4end;
	uint32_t mask = UINT32_MAX << (32-inmask);

	ip4 |= a[0] << 24;
	ip4 |= a[1] << 16;
	ip4 |= a[2] << 8;
	ip4 |= a[3] ;

	ip4end = ip4 | ~mask;
	assert (ip4 == (ip4 & mask));

/*	printf("uip4: \t0x%.8X\na: \t0x", ip4,ip4);
	for (int i=0;i<4;i++) printf("%.2X",a[i]);
	printf("\nmask:\t0x%.8X\nend:\t0x%.8X\n", mask, ip4end);
*/
	while (ip4 <= ip4end) {
		uint8_t *b;
		b=(uint8_t *)&ip4;
		(*serial)++;
		print_backend(tl, *serial, b);
		ip4++;
	}
}
static void
vcc_dir_dns_parse_backend_options(struct tokenlist *tl)
{
	struct fld_spec *fs;
	struct token *t_field;
	double t;
	unsigned u;
	vcc_dir_initialize_defaults();
	fs = vcc_FldSpec(tl,
	    "?port",
	    "?host_header",
	    "?connect_timeout",
	    "?first_byte_timeout",
	    "?between_bytes_timeout",
	    "?max_connections",
	    "?saintmode_threshold",
	    NULL);
	while (tl->t->tok != CSTR) {

		vcc_IsField(tl, &t_field, fs);
		ERRCHK(tl);
		if (vcc_IdIs(t_field, "port")) {
			ExpectErr(tl, CSTR);
			assert(tl->t->dec != NULL);
			b_defaults.port = strdup(tl->t->dec);
			assert(b_defaults.port);
			vcc_NextToken(tl);
			SkipToken(tl, ';');
		} else if (vcc_IdIs(t_field, "host_header")) {
			ExpectErr(tl, CSTR);
			assert(tl->t->dec != NULL);
			b_defaults.hostheader = strdup(tl->t->dec);
			assert(b_defaults.hostheader);
			vcc_NextToken(tl);
			SkipToken(tl, ';');
		} else if (vcc_IdIs(t_field, "connect_timeout")) {
			vcc_TimeVal(tl, &t);
			ERRCHK(tl);
			b_defaults.connect_timeout = t;
			SkipToken(tl, ';');
		} else if (vcc_IdIs(t_field, "first_byte_timeout")) {
			vcc_TimeVal(tl, &t);
			ERRCHK(tl);
			b_defaults.first_byte_timeout = t;
			SkipToken(tl, ';');
		} else if (vcc_IdIs(t_field, "between_bytes_timeout")) {
			vcc_TimeVal(tl, &t);
			ERRCHK(tl);
			b_defaults.between_bytes_timeout = t;
			SkipToken(tl, ';');
		} else if (vcc_IdIs(t_field, "max_connections")) {
			u = vcc_UintVal(tl);
			ERRCHK(tl);
			SkipToken(tl, ';');
			b_defaults.max_connections = u;
		} else if (vcc_IdIs(t_field, "saintmode_threshold")) {
			u = vcc_UintVal(tl);
			/* UINT_MAX == magic number to mark as unset, so
			 * not allowed here.
			 */
			if (u == UINT_MAX) {
				vsb_printf(tl->sb,
				    "Value outside allowed range: ");
				vcc_ErrToken(tl, tl->t);
				vsb_printf(tl->sb, " at\n");
				vcc_ErrWhere(tl, tl->t);
			}
			ERRCHK(tl);
			b_defaults.saint = u;
			SkipToken(tl, ';');
		} else {
			ErrInternal(tl);
			return;
		}

	}
}

/* Parse a list of backends with optional /mask notation, then print out
 * all relevant backends.
 */
static void
vcc_dir_dns_parse_list(struct tokenlist *tl, int *serial)
{
	unsigned char a[4],mask;
	int ret;
	ERRCHK(tl);
	SkipToken(tl, '{');
	if (tl->t->tok != CSTR)
		vcc_dir_dns_parse_backend_options(tl);
	while (tl->t->tok == CSTR) {
		mask = 32;
		ret = sscanf(tl->t->dec, "%hhu.%hhu.%hhu.%hhu",
		    &a[0], &a[1], &a[2], &a[3]);
		assert(ret == 4);
		vcc_NextToken(tl);
		if (tl->t->tok == '/') {
			vcc_NextToken(tl);
			mask = vcc_UintVal(tl);
			ERRCHK(tl);
		}
		vcc_dir_dns_makebackend(tl,serial,a,mask);
		SkipToken(tl,';');
	}
	ExpectErr(tl, '}');
}

void
vcc_ParseDnsDirector(struct tokenlist *tl)
{
	struct token *t_field, *t_be, *t_suffix = NULL;
	double ttl = 60.0;
	int nelem = 0;
	struct fld_spec *fs;
	const char *first;
	char *p;
	dns_first = tl->t;
	tl->fb = tl->fc;
	fs = vcc_FldSpec(tl, "!backend", "?ttl", "?suffix","?list", NULL);

	Fc(tl, 0, "\nstatic const struct vrt_dir_dns_entry "
	    "vddnse_%.*s[] = {\n", PF(tl->t_dir));

	for (; tl->t->tok != '}'; ) {	/* List of members */
		if (tl->t->tok == '{') {
			nelem++;
			first = "";
			t_be = tl->t;
			vcc_ResetFldSpec(fs);

			ExpectErr(tl, '{');
			vcc_NextToken(tl);
			Fc(tl, 0, "\t{");

			while (tl->t->tok != '}') {	/* Member fields */
				vcc_IsField(tl, &t_field, fs);
				ERRCHK(tl);
				if (vcc_IdIs(t_field, "backend")) {
					vcc_ParseBackendHost(tl, nelem, &p);
					ERRCHK(tl);
					AN(p);
					Fc(tl, 0, "%s .host = VGC_backend_%s",
					    first, p);
				} else {
					ErrInternal(tl);
				}
				first = ", ";
			}
			vcc_FieldsOk(tl, fs);
			if (tl->err) {
				vsb_printf(tl->sb, "\nIn member host"
				    " specification starting at:\n");
				vcc_ErrWhere(tl, t_be);
				return;
			}
			Fc(tl, 0, " },\n");
		} else {
			vcc_IsField(tl, &t_field, fs);
			ERRCHK(tl);
			if (vcc_IdIs(t_field, "suffix")) {
				ExpectErr(tl, CSTR);
				t_suffix = tl->t;
				vcc_NextToken(tl);
				ExpectErr(tl, ';');
			} else if (vcc_IdIs(t_field, "ttl")) {
				vcc_RTimeVal(tl, &ttl);
				ExpectErr(tl, ';');
			} else if (vcc_IdIs(t_field, "list")) {
				vcc_dir_dns_parse_list(tl,&nelem);
			}
		}
		vcc_NextToken(tl);
	}
	Fc(tl, 0, "};\n");
	Fc(tl, 0, "\nstatic const struct vrt_dir_dns vgc_dir_priv_%.*s = {\n",
	    PF(tl->t_dir));
	Fc(tl, 0, "\t.name = \"%.*s\",\n", PF(tl->t_dir));
	Fc(tl, 0, "\t.nmember = %d,\n", nelem);
	Fc(tl, 0, "\t.members = vddnse_%.*s,\n", PF(tl->t_dir));
	Fc(tl, 0, "\t.suffix = ");
	if (t_suffix)
		Fc(tl, 0, "%.*s", PF(t_suffix));
	else
		Fc(tl, 0, "\"\"");
	Fc(tl, 0, ",\n");
	Fc(tl, 0, "\t.ttl = %f", ttl);
	Fc(tl, 0, ",\n");
	Fc(tl, 0, "};\n");
	Ff(tl, 0, "\tVRT_fini_dir(cli, VGCDIR(_%.*s));\n", PF(tl->t_dir));
}
