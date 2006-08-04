/*
 * $Id$
 *
 * VCL compiler stuff
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>

#include "sbuf.h"

#include "libvarnish.h"
#include "libvcl.h"
#include "cli.h"
#include "cli_priv.h"
#include "common_cli.h"

#include "mgt.h"

/*--------------------------------------------------------------------*/

static const char *default_vcl =
    "sub default_vcl_recv {\n"
    "    if (req.request != \"GET\" && req.request != \"HEAD\") {\n"
    "        pipe;\n"
    "    }\n"
    "    if (req.http.Expect) {\n"
    "        pipe;\n"
    "    }\n"
    "    if (req.http.Authenticate || req.http.Cookie) {\n"
    "        pass;\n"
    "    }\n"
    "    lookup;\n"
    "}\n"
    "\n"
    "sub default_vcl_hit {\n"
    "    if (!obj.cacheable) {\n"
    "        pass;\n"
    "    }\n"
    "    deliver;\n"
    "}\n"
    "\n"
    "sub default_vcl_miss {\n"
    "    fetch;\n"
    "}\n"
    "\n"
    "sub default_vcl_fetch {\n"
    "    if (!obj.valid) {\n"
    "        error;\n"
    "    }\n"
    "    if (!obj.cacheable) {\n"
    "        insert_pass;\n"
    "    }\n"
    "    insert;\n"
    "}\n"
    "sub default_vcl_timeout {\n"
    "    discard;\n"
    "}\n";

/*--------------------------------------------------------------------*/

char *
mgt_vcc_default(const char *bflag)
{
	char *buf, *vf;
	const char *p, *q;
	struct sbuf *sb;

	/*
	 * XXX: should do a "HEAD /" on the -b argument to see that
	 * XXX: it even works.  On the other hand, we should do that
	 * XXX: for all backends in the cache process whenever we
	 * XXX: change config, but for a complex VCL, it might not be
	 * XXX: a bug for a backend to not reply at that time, so then
	 * XXX: again: we should check it here in the "trivial" case.
	 */
	p = strchr(bflag, ' ');
	if (p != NULL) {
		q = p + 1;
	} else {
		p = strchr(bflag, '\0');
		assert(p != NULL);
		q = "http";
	}
	
	buf = NULL;
	asprintf(&buf,
	    "backend default {\n"
	    "    set backend.host = \"%*.*s\";\n"
	    "    set backend.port = \"%s\";\n"
	    "}\n", (int)(p - bflag), (int)(p - bflag), bflag, q);
	assert(buf != NULL);
	sb = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	assert(sb != NULL);
	vf = VCC_Compile(sb, buf, NULL);
	sbuf_finish(sb);
	if (sbuf_len(sb) > 0) {
		fprintf(stderr, "%s", sbuf_data(sb));
		free(buf);
		sbuf_delete(sb);
		return (NULL);
	}
	sbuf_delete(sb);
	free(buf);
	return (vf);
}

/*--------------------------------------------------------------------*/

char *
mgt_vcc_file(const char *fflag)
{
	char *vf;
	struct sbuf *sb;

	sb = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	assert(sb != NULL);
	vf = VCC_CompileFile(sb, fflag);
	sbuf_finish(sb);
	if (sbuf_len(sb) > 0) {
		fprintf(stderr, "%s", sbuf_data(sb));
		sbuf_delete(sb);
		return (NULL);
	}
	sbuf_delete(sb);
	return (vf);
}

/*--------------------------------------------------------------------*/

void
mgt_vcc_init(void)
{

	VCC_InitCompile(default_vcl);
}


#if 0
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "mgt.h"

static void
m_cli_func_config_inline(struct cli *cli, char **av, void *priv)
{
	char *vf;
	struct sbuf *sb;

	(void)priv;

	sb = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	assert(sb != NULL);
	vf = VCC_Compile(sb, av[3], NULL);
	sbuf_finish(sb);
	if (sbuf_len(sb) > 0) {
		cli_out(cli, "%s", sbuf_data(sb));
		sbuf_delete(sb);
		return;
	}
	sbuf_delete(sb);
	cli_suspend(cli);
	mgt_child_request(cli_passthrough_cb, cli, NULL,
	    "config.load %s %s", av[2], vf);
}

/* XXX: m prefix to avoid name clash */
static void
m_cli_func_config_load(struct cli *cli, char **av, void *priv)
{
	char *vf;
	struct sbuf *sb;

	(void)priv;

	sb = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	assert(sb != NULL);
	vf = VCC_CompileFile(sb, av[3]);
	sbuf_finish(sb);
	if (sbuf_len(sb) > 0) {
		cli_out(cli, "%s", sbuf_data(sb));
		sbuf_delete(sb);
		return;
	}
	sbuf_delete(sb);
	cli_suspend(cli);
	mgt_child_request(cli_passthrough_cb, cli, NULL,
	    "config.load %s %s", av[2], vf);
}

#endif
