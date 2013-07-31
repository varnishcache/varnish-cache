// 
// http://tools.ietf.org/html/draft-fielding-http-key-00
//

#include <stdio.h>
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

#define DEBUG 1

int hexDump (char *desc, void *addr, int len) {
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
    return 0;
}

int
key_ParseMatcher(const char *s, struct vsb **sb) {
	const char *p = s;
	const char *e;
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

/*
 * Find length of a key entry
 */
static unsigned
key_len(const uint8_t *p)
{
	unsigned l = vbe16dec(p);

	return (3 + p[3] + 2 + (l == 0xffff ? 0 : l));
}

int
KEY_Create(struct busyobj *bo, struct vsb **psb)
{
	DEBUG && printf("KEY_Create(bo: %p, psb: %p)\n", bo, *psb);
	char *v, *h, *e, *p, *q, *m, *mm, *me;
	struct vsb *sb, *sbh, *sbm;
	unsigned l;
	int matcher = 0;
	int error = 0;

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

		if (q - p > INT8_MAX) {
			VSLb(bo->vsl, SLT_Error,
			    "Key header name length exceeded");
			error = 1;
			break;
		}

		/* Build header matching string */
		VSB_clear(sbh);
		VSB_printf(sbh, "%c%.*s:%c",
		    (char)(1 + (q - p)), (int)(q - p), p, 0);
		AZ(VSB_finish(sbh));

		DEBUG && printf(" - Entry: %.*s\n", (int)(q - p), p);

		// Using matchers
		if (*q == ';') {
			VSB_clear(sbm);
			int ksize = key_ParseMatcher(q, &sbm);
			if (ksize > 0)
			    q += ksize;
			else {
			    // TODO: Cleanup allocations
			    printf("ERROR\n");
			    error = 1;
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
		if (*q != ',') {
			VSLb(bo->vsl, SLT_Error, "Malformed Key header");
			error = 1;
			break;
		}

		p = q;
	}

	if (error) {
		VSB_delete(sbh);
		VSB_delete(sb);
		return (-1);
	}

	/* Terminate key matching string */
	VSB_printf(sb, "%c%c%c%c", 0xff, 0xff, 0, 0);

	VSB_delete(sbh);
	AZ(VSB_finish(sb));

	if (KEY_Match(bo->bereq, VSB_data(sb)) == 0) {
	    printf("That doesn't match this req wth\n");
	    return -1;
	}

	*psb = sb;
	DEBUG && printf("KEY_Create(bo: %p, psb: %p) = %zu\n", bo, *psb, VSB_len(sb));
	DEBUG && hexDump("key", VSB_data(sb), VSB_len(sb));
	return (VSB_len(sb));
}

void
KEY_Validate(const uint8_t *key)
{
	DEBUG && hexDump("key", key, 32);
	while (key[3] != 0) {
		assert(strlen((const char*)key+4) == key[3]);
		key += key_len(key);
	}
}

int word_match(char *param, char *string) {
  char *p = strstr(string, param);

  if (p == 0)
    return 0;

  if (p != string)
    if (p[-1] != ' ' && p[-1] != ',')
      return 0;

  char *r = p + strlen(param);
  if (r < string + strlen(string))
    if (r[0] != ' ' && r[0] != ',')
      return 0;

  return 1;
}

int
KEY_Match(struct http *http, const uint8_t *key)
{
	DEBUG && printf("KEY_Match(http: %p, key: %p)\n", http, key);

	char *h;
	int i;

	while (key[3]) {
		// Exception for gzip
		if (cache_param->http_gzip_support &&
		    !strcasecmp(H_Accept_Encoding, (const char*) key + 3)) {
		// Exact match
		} else if (key[2] == 0) {
			char *e;
			unsigned l = vbe16dec(key);

			DEBUG && printf(" Header (Exact): %s\n", key + 4);

			i = http_GetHdr(http, (const char*)(key+3), &h);

			if (l == 0xFFFF) {
			    // Expect missing
			    if (i == 0) {
				// Expected missing, is missing
				DEBUG && printf("   * Expected missing, is missing\n");
			    } else {
				// Expected missing, is present
				DEBUG && printf("   * Expected missing, is present\n");
				return 0;
			    }
			} else {
			    // Expect present
			    const char *value = key + 4 + key[3] + 1;
			    DEBUG && printf(" - Value: %.*s\n", l, value);

			    if (i == 0) {
				// Expected present, is missing
				DEBUG && printf("   * Expected present, is missing\n");
				return 0;
			    } else {
				// Expected present, is present
				DEBUG && printf("   * Expected present, is present\n");

				AZ(vct_issp(*h));
				/* Trim trailing space */
				e = strchr(h, '\0');
				while (e > h && vct_issp(e[-1]))
					e--;

				if (l != (int)(e - h)) {
				    // Different lengths, no match
				    DEBUG && printf("   * Different lengths, no match\n");
				    return 0;
				} else {
				    if (memcmp(h, value, l) == 0) {
					// Same length, match
					DEBUG && printf("   * Same length, match\n");
				    } else {
					// Same length, no match
					DEBUG && printf("   * Same length, no match\n");
					return 0;
				    }
				}
			    }
			}

		// Matcher match
		} else if (key[2] == 1) {
			DEBUG && printf(" Header (Matcher): %s\n", key + 4);
			char *e;
			unsigned l = vbe16dec(key);

			i = http_GetHdr(http, (const char*)(key+3), &h);

			// TODO: Perhaps not matcher should allow this
			if (i == 0)
			    return 0;

			const char *matcher = key + 4 + key[3] + 1;

			while (*matcher != 0 && *matcher != -1) {
			    if (*matcher == M_WORD) {
				DEBUG && printf("  - Word: %s ", matcher + 1);
				if (word_match(matcher + 1, h) != 1) {
				    printf("NG\n");
				    return 0;
				}
				printf("OK\n");
				matcher += strlen(matcher) + 1;
			    } else if (*matcher == M_CASE) {
				DEBUG && printf("  - Case\n");
				matcher++;
			    } else {
				DEBUG && printf("UNKNOWN MATCHER (%d)\n", *matcher);
				return 0;
			    }
			}
		}

		key += key_len(key);
	}

	DEBUG && printf("KEY_Match(http: %p, key: %p) = 1\n", http, key);
	return 1;
}
