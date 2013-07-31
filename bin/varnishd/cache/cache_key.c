//#include "config.h"

#include "cache.h"

#include "vct.h"
#include "vend.h"

#define M_WORD		1
#define M_SUBSTRING	2
#define M_BEGINNING	3
#define M_PARAMETER	4
#define M_CASE		5
#define M_NOT		6

void hexDump (char *desc, void *addr, int len) {
    int i;
    unsigned char buff[17];
    unsigned char *pc = addr;

    // Output description if given.
    if (desc != NULL)
        printf ("%s:\n", desc);

    // Process every byte in the data.
    for (i = 0; i < len; i++) {
        // Multiple of 16 means new line (with line offset).

        if ((i % 16) == 0) {
            // Just don't print ASCII for the zeroth line.
            if (i != 0)
                printf ("  %s\n", buff);

            // Output the offset.
            printf ("  %04x ", i);
        }

        // Now the hex code for the specific character.
        printf (" %02x", pc[i]);

        // And store a printable ASCII character for later.
        if ((pc[i] < 0x20) || (pc[i] > 0x7e))
            buff[i % 16] = '.';
        else
            buff[i % 16] = pc[i];
        buff[(i % 16) + 1] = '\0';
    }

    // Pad out last line if not exactly 16 characters.
    while ((i % 16) != 0) {
        printf ("   ");
        i++;
    }

    // And print the final ASCII bit.
    printf ("  %s\n", buff);
}

int
key_ParseMatcher(const char *s, struct vsb **sb) {
	char *p = s;
	char *e;
	while (*p == ';') {
		if (*p != ';')
			return -1;

		p++;

		if (strncmp(p, "w=\"", 3) == 0) {
			printf("   WORD: (");
			p += 3;
			for (e = p; *e != '\"'; e++)
				continue;
			printf("%.*s)\n", (int)(e - p), p);
			VSB_printf(*sb, "%c%.*s%c", M_WORD, (int)(e -p), p, 0);
			p = e + 1;
		} else if (strncmp(p, "s=\"", 3) == 0) {
			printf("   SUBSTRING: (");
			p += 3;
			for (e = p; *e != '\"'; e++)
				continue;
			printf("%.*s)\n", (int)(e - p), p);
			VSB_printf(*sb, "%c%.*s%c", M_SUBSTRING, (int)(e -p), p, 0);
			p = e + 1;
		} else if (strncmp(p, "b=\"", 3) == 0) {
			printf("   BEGINNING SUBSTRING: (");
			p += 3;
			for (e = p; *e != '\"'; e++)
				continue;
			printf("%.*s)\n", (int)(e - p), p);
			VSB_printf(*sb, "%c%.*s%c", M_BEGINNING, (int)(e -p), p, 0);
			p = e + 1;
		} else if (*p == 'c') {
			printf("   CASE\n");
			VSB_printf(*sb, "%c", M_CASE);
			p++;
		} else if (*p == 'n') {
			printf("   NOT\n");
			VSB_printf(*sb, "%c", M_NOT);
			p++;
		} else {
			// Invalid matcher
			return -1;
		}
	}
	return p - s;
}

int
KEY_Create(struct busyobj *bo, struct vsb **psb)
{
	printf("KEY_Create(bo: %p, psb: %p)\n", bo, *psb);
	char *v, *h, *e, *p, *q, *m, *mm, *me;
	struct vsb *sb, *sbh, *sbm;
	unsigned l;
	int matcher = 0;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->bereq, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(bo->beresp, HTTP_MAGIC);
	AN(psb);
	AZ(*psb);

	if (!http_GetHdr(bo->beresp, "\4Key:", &v))
		return (0);

	sb = VSB_new_auto();
	AN(sb);

	/* For header matching strings */
	sbh = VSB_new_auto();
	AN(sbh);

	/* For matcher strings */
	sbm = VSB_new_auto();
	AN(sbm);

	for (p = v; *p; p++) {
		/* Find next header-name */
		if (vct_issp(*p))
			continue;
		for (q = p; *q && !vct_issp(*q) && *q != ',' && *q != ';'; q++)
			continue;

		/* Build header matching string */
		VSB_clear(sbh);
		VSB_printf(sbh, "%c%.*s:%c",
		    (char)(1 + (q - p)), (int)(q - p), p, 0);
		AZ(VSB_finish(sbh));

		printf(" - Entry: %.*s\n", (int)(q - p), p);

		// Using matchers
		if (*q == ';') {
			VSB_clear(sbm);
			int ksize = key_ParseMatcher(q, &sbm);
			if (ksize > 0)
			    q += ksize;
			else {
			    // TODO: Cleanup allocations
			    printf("ERROR\n");
			    return 0;
			}
			AZ(VSB_finish(sbm));
			l = VSB_len(sbm);
			e = h;
			matcher = 1;
		// Exact match
		} else {
			matcher = 0;
			if (http_GetHdr(bo->bereq, VSB_data(sbh), &h)) {
				AZ(vct_issp(*h));
				/* Trim trailing space */
				e = strchr(h, '\0');
				while (e > h && vct_issp(e[-1]))
					e--;
				/* Encode two byte length and contents */
				l = e - h;
				if (l > 0xffff - 1) {
					VSLb(bo->vsl, SLT_Error,
					    "Vary header maximum length exceeded");
					//error = 1;
					break;
				}
			} else {
				e = h;
				l = 0xffff;
			}
		}

		// Length
		VSB_printf(sb, "%c%c", (int)(l >> 8), (int)(l & 0xff));
		// Type
		VSB_printf(sb, "%c", matcher);
		/* Append to key matching string */
		VSB_bcat(sb, VSB_data(sbh), VSB_len(sbh));
		if (e != h)
			VSB_bcat(sb, h, e - h);
		if (matcher == 1)
			VSB_bcat(sb, VSB_data(sbm), VSB_len(sbm));

		if (*q == '\0')
			break;

		p = q;
	}

	/* Terminate key matching string */
	VSB_printf(sb, "%c%c%c", 0xff, 0xff, 0);

	VSB_delete(sbh);
	AZ(VSB_finish(sb));
	*psb = sb;
	printf("KEY_Create(bo: %p, psb: %p) = %d\n", bo, *psb, VSB_len(sb));
	hexDump("key", VSB_data(sb), VSB_len(sb));
	return (VSB_len(sb));
}

void
KEY_Prep(struct req *req)
{
	req->key_b = req->vary_b;
	req->key_l = req->vary_l;
	req->key_e = req->vary_e;
}

void
KEY_Finish(struct req *req, struct busyobj *bo)
{
	if (bo != NULL) {
		CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
		KEY_Validate(req->key_b);
		if (req->key_l != NULL) {
			bo->key = WS_Copy(bo->ws,
			    req->key_b, req->key_l - req->key_b);
			AN(bo->key);
			KEY_Validate(bo->key);
		} else
			bo->key = NULL;
	}
	//WS_Release(req->ws, 0);
	req->key_b = NULL;
	req->key_l = NULL;
	req->key_e = NULL;
}

/*
 * Find length of a key entry
 */
static unsigned
key_len(const uint8_t *p)
{
	unsigned l = vbe16dec(p);

	return (3 + p[3] + 2 + (l == 0xffff ? 0 : l));
}

/*
 * Compare two key entries
 */
static int
key_cmp(const uint8_t *v1, const uint8_t *v2)
{
	unsigned retval = 0;

	//hexDump("key_cmp(v1)", v1, key_len(v1));
	//hexDump("key_cmp(v2)", v2, key_len(v2));

	if (!memcmp(v1, v2, key_len(v1))) {
		printf("    same same\n");
		/* Same same */
		retval = 0;
	} else if (memcmp(v1 + 2, v2 + 2, v1[3] + 2)) {
		printf("    diff header\n");
		/* Different header */
		retval = 1;
	} else if (cache_param->http_gzip_support &&
	    !strcasecmp(H_Accept_Encoding, (const char*) v1 + 2)) {
		printf("    accept enc\n");
		/*
		 * If we do gzip processing, we do not key on Accept-Encoding,
		 * because we want everybody to get the gzip'ed object, and
		 * varnish will gunzip as necessary.  We implement the skip at
		 * check time, rather than create time, so that object in
		 * persistent storage can be used with either setting of
		 * http_gzip_support.
		 */
		retval = 0;
	} else {
		printf("    same header diff content\n");
		/* Same header, different content */
		retval = 2;
	}

	return retval;
}

int
KEY_Match(struct req *req, const uint8_t *key)
{
	printf("KEY_Match(req: %p, key: %p)\n", req, key);

	uint8_t *vsp = req->key_b;
	char *h;
	int i;

	AN(vsp);
	while (key[3]) {
		// Exact match
		if (key[2] == 0) {
			printf(" - Exact: %s\n", key+4);
			i = key_cmp(key, vsp);
			if (i == 1) {
				/*
				 * Different header, build a new entry,
				 * then compare again with that new entry.
				 */

				//ln = 2 + key[3] + 2;
				i = http_GetHdr(req->http, (const char*)(key+3), &h);
				printf("    - Got %d bytes of %s\n", i, h);
			}
		// Matcher match
		} else if (key[2] == 1) {
			printf(" - Matcher: %s\n", key+4);
		}

		key += key_len(key);
	}

	printf("KEY_Match(req: %p, key: %p) = 0\n", req, key);
	return 0;
}

void
KEY_Validate(const uint8_t *key)
{

	while (key[3] != 0) {
		assert(strlen((const char*)key+4) == key[3]);
		key += key_len(key);
	}
}
