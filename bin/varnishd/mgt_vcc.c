/*
 * $Id$
 *
 * VCL compiler stuff
 */

#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <sys/types.h>

#include "sbuf.h"
#include "queue.h"

#include "libvarnish.h"
#include "libvcl.h"
#include "cli.h"
#include "cli_priv.h"
#include "common_cli.h"

#include "mgt.h"
#include "mgt_cli.h"

struct vcls {
	TAILQ_ENTRY(vcls)	list;
	char 			*name;
	char			*fname;
	int			active;
};


static TAILQ_HEAD(, vcls) vclhead = TAILQ_HEAD_INITIALIZER(vclhead);

static pthread_mutex_t		vcc_mtx;

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

static struct vcls *
mgt_vcc_add(const char *name, char *file)
{
	struct vcls *vp;

	vp = calloc(sizeof *vp, 1);
	assert(vp != NULL);
	vp->name = strdup(name);
	vp->fname = file;
	AZ(pthread_mutex_lock(&vcc_mtx));
	TAILQ_INSERT_TAIL(&vclhead, vp, list);
	AZ(pthread_mutex_unlock(&vcc_mtx));
	return (vp);
}

static void
mgt_vcc_del(struct vcls *vp)
{
	TAILQ_REMOVE(&vclhead, vp, list);
	printf("unlink %s\n", vp->fname);
	AZ(unlink(vp->fname));	/* XXX assert for now */
	free(vp->fname);
	free(vp->name);
	free(vp);
}

static int
mgt_vcc_delbyname(const char *name)
{
	struct vcls *vp;

	TAILQ_FOREACH(vp, &vclhead, list) {
		if (!strcmp(name, vp->name)) {
			mgt_vcc_del(vp);
			return (0);
		}
	}
	return (1);
}

/*--------------------------------------------------------------------*/

int
mgt_vcc_default(const char *bflag, const char *fflag)
{
	char *buf, *vf;
	const char *p, *q;
	struct sbuf *sb;
	struct vcls *vp;

	sb = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	assert(sb != NULL);
	if (bflag != NULL) {
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
		vf = VCC_Compile(sb, buf, NULL);
		free(buf);
	} else {
		vf = VCC_CompileFile(sb, fflag);
	}
	sbuf_finish(sb);
	if (sbuf_len(sb) > 0) {
		fprintf(stderr, "%s", sbuf_data(sb));
		sbuf_delete(sb);
		return (1);
	}
	sbuf_delete(sb);
	vp = mgt_vcc_add("boot", vf);
	vp->active = 1;
	return (0);
}

/*--------------------------------------------------------------------*/

int
mgt_push_vcls_and_start(int *status, char **p)
{
	struct vcls *vp;

	AZ(pthread_mutex_lock(&vcc_mtx));
	TAILQ_FOREACH(vp, &vclhead, list) {
		if (mgt_cli_askchild(status, p,
		    "config.load %s %s\n", vp->name, vp->fname))
			return (1);
		if (vp->active)
		if (mgt_cli_askchild(status, p,
		    "config.use %s\n", vp->name, vp->fname))
			return (1);
	}
	AZ(pthread_mutex_unlock(&vcc_mtx));
	if (mgt_cli_askchild(status, p, "start\n"))
		return (1);
	return (0);
}

/*--------------------------------------------------------------------*/

static
void
mgt_vcc_atexit(void)
{
	struct vcls *vp;

	if (getpid() != mgt_pid)
		return;
	AZ(pthread_mutex_lock(&vcc_mtx));
	while (1) {
		vp = TAILQ_FIRST(&vclhead);
		if (vp == NULL)
			break;
		mgt_vcc_del(vp);
	}
	AZ(pthread_mutex_unlock(&vcc_mtx));
}

void
mgt_vcc_init(void)
{

	AZ(pthread_mutex_init(&vcc_mtx, NULL));
	VCC_InitCompile(default_vcl);
	AZ(atexit(mgt_vcc_atexit));
}

/*--------------------------------------------------------------------*/

void
mcf_config_inline(struct cli *cli, char **av, void *priv)
{
	char *vf, *p;
	struct sbuf *sb;
	int status;

	(void)priv;

	sb = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	assert(sb != NULL);
	vf = VCC_Compile(sb, av[3], NULL);
	sbuf_finish(sb);
	if (sbuf_len(sb) > 0) {
		cli_out(cli, "%s", sbuf_data(sb));
		sbuf_delete(sb);
		cli_result(cli, CLIS_PARAM);
		return;
	}
	sbuf_delete(sb);
	if (mgt_cli_askchild(&status, &p, "config.load %s %s\n", av[2], vf)) {
		cli_result(cli, status);
		cli_out(cli, "%s", p);
		free(p);
		return;
	}
	mgt_vcc_add(av[2], vf);
}

void
mcf_config_load(struct cli *cli, char **av, void *priv)
{
	char *vf;
	struct sbuf *sb;
	int status;
	char *p;

	(void)priv;

	sb = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	assert(sb != NULL);
	vf = VCC_CompileFile(sb, av[3]);
	sbuf_finish(sb);
	if (sbuf_len(sb) > 0) {
		cli_out(cli, "%s", sbuf_data(sb));
		sbuf_delete(sb);
		cli_result(cli, CLIS_PARAM);
		return;
	}
	sbuf_delete(sb);
	if (mgt_cli_askchild(&status, &p, "config.load %s %s\n", av[2], vf)) {
		cli_result(cli, status);
		cli_out(cli, "%s", p);
		free(p);
		return;
	}
	mgt_vcc_add(av[2], vf);
}

static struct vcls *
mcf_find_vcl(struct cli *cli, const char *name)
{
	struct vcls *vp;

	TAILQ_FOREACH(vp, &vclhead, list)
		if (!strcmp(vp->name, name))
			break;
	if (vp == NULL) {
		cli_result(cli, CLIS_PARAM);
		cli_out(cli, "No configuration named %s known.", name);
	}
	return (vp);
}

void
mcf_config_use(struct cli *cli, char **av, void *priv)
{
	int status;
	char *p;
	struct vcls *vp;

	(void)priv;
	AZ(pthread_mutex_lock(&vcc_mtx));
	vp = mcf_find_vcl(cli, av[2]);
	if (vp != NULL && vp->active == 0) {
		if (mgt_cli_askchild(&status, &p, "config.use %s\n", av[2])) {
			cli_result(cli, status);
			cli_out(cli, "%s", p);
			free(p);
		} else {
			vp->active = 2;
			TAILQ_FOREACH(vp, &vclhead, list) {
				if (vp->active == 1)
					vp->active = 0;
				else if (vp->active == 2)
					vp->active = 1;
			}
		}
	}
	AZ(pthread_mutex_unlock(&vcc_mtx));
}

void
mcf_config_discard(struct cli *cli, char **av, void *priv)
{
	int status;
	char *p;
	struct vcls *vp;
	(void)priv;
	AZ(pthread_mutex_lock(&vcc_mtx));
	vp = mcf_find_vcl(cli, av[2]);
	if (vp != NULL && vp->active) {
		cli_result(cli, CLIS_PARAM);
		cli_out(cli, "Cannot discard active VCL program\n");
	} else if (vp != NULL) {
		if (mgt_cli_askchild(&status, &p,
		    "config.discard %s\n", av[2])) {
			cli_result(cli, status);
			cli_out(cli, "%s", p);
			free(p);
		} else {
			AZ(mgt_vcc_delbyname(av[2]));
		}
	}
	AZ(pthread_mutex_unlock(&vcc_mtx));
}
