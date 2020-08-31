
#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "config.h"

#include "vtc.h"

struct vtc_sess {
	unsigned		magic;
#define VTC_SESS_MAGIC		0x932bd565
	struct vtclog		*vl;
	char			*name;
	int			repeat;
	int			keepalive;

	ssize_t			rcvbuf;
};

struct thread_arg {
	unsigned		magic;
#define THREAD_ARG_MAGIC	0xd5dc5f1c
	void			*priv;
	sess_conn_f		*conn_f;
	sess_disc_f		*disc_f;
	const char		*listen_addr;
	struct vtc_sess		*vsp;
	int			*asocket;
	const char		*spec;
};

struct vtc_sess *
Sess_New(struct vtclog *vl, const char *name)
{
	struct vtc_sess *vsp;

	ALLOC_OBJ(vsp, VTC_SESS_MAGIC);
	AN(vsp);
	vsp->vl = vl;
	REPLACE(vsp->name, name);
	vsp->repeat = 1;
	return (vsp);
}

void
Sess_Destroy(struct vtc_sess **vspp)
{
	struct vtc_sess *vsp;

	TAKE_OBJ_NOTNULL(vsp, vspp, VTC_SESS_MAGIC);
	REPLACE(vsp->name, NULL);
	FREE_OBJ(vsp);
}

int
Sess_GetOpt(struct vtc_sess *vsp, char * const **avp)
{
	char * const *av;
	int rv = 0;

	CHECK_OBJ_NOTNULL(vsp, VTC_SESS_MAGIC);
	AN(avp);
	av = *avp;
	AN(*av);
	if (!strcmp(*av, "-rcvbuf")) {
		AN(av[1]);
		vsp->rcvbuf = atoi(av[1]);
		av += 1;
		rv = 1;
	} else if (!strcmp(*av, "-repeat")) {
		AN(av[1]);
		vsp->repeat = atoi(av[1]);
		av += 1;
		rv = 1;
	} else if (!strcmp(*av, "-keepalive")) {
		vsp->keepalive = 1;
		rv = 1;
	}
	*avp = av;
	return (rv);
}

int
sess_process(struct vtclog *vl, const struct vtc_sess *vsp,
    const char *spec, int sock, int *sfd, const char *addr)
{
	int rv;

	CHECK_OBJ_NOTNULL(vsp, VTC_SESS_MAGIC);

	rv = http_process(vl, spec, sock, sfd, addr, vsp->rcvbuf);
	return (rv);
}

static void *
sess_thread(void *priv)
{
	struct vtclog *vl;
	struct vtc_sess *vsp;
	struct thread_arg ta, *tap;
	int i, fd;

	CAST_OBJ_NOTNULL(tap, priv, THREAD_ARG_MAGIC);
	ta = *tap;
	FREE_OBJ(tap);

	vsp = ta.vsp;
	CHECK_OBJ_NOTNULL(vsp, VTC_SESS_MAGIC);
	vl = vtc_logopen(vsp->name);
	pthread_cleanup_push(vtc_logclose, vl);

	assert(vsp->repeat > 0);
	vtc_log(vl, 2, "Started on %s (%u iterations%s)", ta.listen_addr,
		vsp->repeat, vsp->keepalive ? " using keepalive" : "");
	for (i = 0; i < vsp->repeat; i++) {
		fd = ta.conn_f(ta.priv, vl);
		if (! vsp->keepalive)
			fd = sess_process(vl, ta.vsp, ta.spec, fd, ta.asocket, ta.listen_addr);
		else
			while (fd >= 0 && i++ < vsp->repeat)
				fd = sess_process(vl, ta.vsp, ta.spec, fd,
				    ta.asocket, ta.listen_addr);
		ta.disc_f(ta.priv, vl, &fd);
	}
	vtc_log(vl, 2, "Ending");
	pthread_cleanup_pop(0);
	vtc_logclose(vl);
	return (NULL);
}

pthread_t
Sess_Start_Thread(
    void *priv,
    struct vtc_sess *vsp,
    sess_conn_f *conn,
    sess_disc_f *disc,
    const char *listen_addr,
    int *asocket,
    const char *spec
)
{
	struct thread_arg *ta;
	pthread_t pt;

	AN(priv);
	CHECK_OBJ_NOTNULL(vsp, VTC_SESS_MAGIC);
	AN(conn);
	AN(disc);
	AN(listen_addr);
	ALLOC_OBJ(ta, THREAD_ARG_MAGIC);
	AN(ta);
	ta->priv = priv;
	ta->vsp = vsp;

	ta->conn_f = conn;
	ta->disc_f = disc;
	ta->listen_addr = listen_addr;
	ta->asocket = asocket;
	ta->spec = spec;
	AZ(pthread_create(&pt, NULL, sess_thread, ta));
	return (pt);
}
