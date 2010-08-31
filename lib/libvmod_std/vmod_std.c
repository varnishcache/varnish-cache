#include <ctype.h>
#include <stdarg.h>
#include "vrt.h"
#include "../../bin/varnishd/cache.h"

#include "vcc_if.h"

static const char *
vmod_updown(struct sess *sp, int up, const char *s, va_list ap)
{
	unsigned u;
	char *b, *e;
	const char *p;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	u = WS_Reserve(sp->wrk->ws, 0);
	e = b = sp->wrk->ws->f;
	e += u;
	p = s;
	while (p != vrt_magic_string_end && b < e) {
		for (; b < e && *p != '\0'; p++)
			if (up)
				*b++ = toupper(*p);
			else
				*b++ = tolower(*p);
		p = va_arg(ap, const char *);
	}
	if (b < e)
		*b = '\0';
	b++;
	if (b > e) {
		WS_Release(sp->wrk->ws, 0);
		return (NULL);
	} else {
		e = b;
		b = sp->wrk->ws->f;
		WS_Release(sp->wrk->ws, e - b);
		return (b);
	}
}

const char *
vmod_toupper(struct sess *sp, const char *s, ...)
{
	const char *p;
	va_list ap;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	va_start(ap, s);
	p = vmod_updown(sp, 1, s, ap);
	va_end(ap);
	return (p);
}

const char *
vmod_tolower(struct sess *sp, void **vcl_priv, const char *s, ...)
{
	const char *p;
	va_list ap;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	assert(*vcl_priv == (void *)8);
	va_start(ap, s);
	p = vmod_updown(sp, 0, s, ap);
	va_end(ap);
	return (p);
}

int
meta_function(void **priv, const struct VCL_conf *cfg)
{
	if (cfg != NULL) {
		// Initialization in new VCL program
		// Hang any private data/state off *priv
		*priv = (void *)8;
	} else {
		// Cleaup in new VCL program
		// Cleanup/Free any private data/state hanging of *priv
		assert(*priv == (void *)8);
		*priv = NULL;
	}
	return (0);
}
