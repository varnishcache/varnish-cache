
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <netdb.h>

#include "libvarnish.h"
#include "event.h"
#include "queue.h"


/*--------------------------------------------------------------------
 * stubs to survice linkage
 */
 
#include "cli_priv.h"

struct cli;

void cli_out(struct cli *cli, const char *fmt, ...) { (void)cli; (void)fmt; abort(); }
void cli_param(struct cli *cli) { (void)cli; abort(); }
void cli_result(struct cli *cli, unsigned res) { (void)cli; (void)res; abort(); }

/*--------------------------------------------------------------------*/

static struct event_base *eb;


static void Pause(void);
static void Resume(void);

/*--------------------------------------------------------------------*/

static int serv_sock = -1;
static struct event e_acc;
static struct bufferevent *e_racc;

struct serv {
	TAILQ_ENTRY(serv)	list;
	char			*data;
	int			close;
};

static TAILQ_HEAD(,serv) serv_head = TAILQ_HEAD_INITIALIZER(serv_head);

static void
rd_acc(struct bufferevent *bev, void *arg)
{
	char *p;
	struct serv *sp;
	int *ip;

	ip = arg;
	while (1) {
		p = evbuffer_readline(bev->input);
		if (p == NULL)
			return;
		printf("B: <<%s>>\n", p);
		if (*p == '\0') {
			sp = TAILQ_FIRST(&serv_head);
			assert(sp != NULL);
			write(*ip, sp->data, strlen(sp->data));
			if (sp->close)
				shutdown(*ip, SHUT_WR);
			if (TAILQ_NEXT(sp, list) != NULL) {
				TAILQ_REMOVE(&serv_head, sp, list);
				free(sp->data);	
				free(sp);
			}
		}
	}
}

static void
ex_acc(struct bufferevent *bev, short what, void *arg)
{
	int *ip;

	(void)what;
	ip = arg;
	bufferevent_disable(bev, EV_READ);
	bufferevent_free(bev);
	close(*ip);
	free(ip);
}

static void
acc_sock(int fd, short event, void *arg)
{
	struct sockaddr addr[2];	/* XXX: IPv6 hack */
	socklen_t l;
	struct linger linger;
	int *ip;

	ip = calloc(sizeof *ip, 1);
	(void)event;
	(void)arg;
	l = sizeof addr;
	fd = accept(fd, addr, &l);
	if (fd < 0) {
		perror("accept");
		exit (2);
	}
#ifdef SO_LINGER /* XXX Linux ? */
	linger.l_onoff = 0;
	linger.l_linger = 0;
	assert(setsockopt(fd, SOL_SOCKET, SO_LINGER,
	   &linger, sizeof linger) == 0);
#endif
	*ip = fd;
	e_racc = bufferevent_new(fd, rd_acc, NULL, ex_acc, ip);
	assert(e_racc != NULL);
	bufferevent_base_set(eb, e_racc);
	bufferevent_enable(e_racc, EV_READ);
}

static void
open_serv_sock(void)
{
	struct addrinfo ai, *r0, *r1;
	int i, j, s = -1;

	memset(&ai, 0, sizeof ai);
	ai.ai_family = PF_UNSPEC;
	ai.ai_socktype = SOCK_STREAM;
	ai.ai_flags = AI_PASSIVE;
	i = getaddrinfo("localhost", "8081", &ai, &r0);

	if (i) {
		fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(i));
		return;
	}

	for (r1 = r0; r1 != NULL; r1 = r1->ai_next) {
		s = socket(r1->ai_family, r1->ai_socktype, r1->ai_protocol);
		if (s < 0)
			continue;
		j = 1;
		i = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &j, sizeof j);
		assert(i == 0);

		i = bind(s, r1->ai_addr, r1->ai_addrlen);
		if (i != 0) {
			perror("bind");
			continue;
		}
		assert(i == 0);
		serv_sock = s;
		break;
	}
	freeaddrinfo(r0);
	if (s < 0) {
		perror("bind");
		exit (2);
	}

	listen(s, 16);

	event_set(&e_acc, s, EV_READ | EV_PERSIST, acc_sock, NULL);
	event_base_set(eb, &e_acc);
	event_add(&e_acc, NULL);
}

static void
cmd_serve(char **av)
{
	struct serv *sp;
	int i;

	for (i = 0; av[i] != NULL; i++) {
		sp = calloc(sizeof *sp, 1);
		assert(sp != NULL);
		if (av[i][0] == '!') {
			sp->close = 1;
			sp->data = strdup(av[i] + 1);
		} else {
			sp->data = strdup(av[i]);
		}
		assert(sp->data != NULL);
		TAILQ_INSERT_TAIL(&serv_head, sp, list);
	}
}

/*--------------------------------------------------------------------*/

static struct bufferevent *e_pipe2;
static int pipe1[2];
static int pipe2[2];
static pid_t child;

/*--------------------------------------------------------------------*/

static void
cli_write(const char *s)
{

	write(pipe1[1], s, strlen(s));
}

/*--------------------------------------------------------------------*/

static void
rd_pipe2(struct bufferevent *bev, void *arg)
{
	char *p;

	(void)arg;
	while (1) {
		p = evbuffer_readline(bev->input);
		if (p == NULL)
			return;
		printf("V: <<%s>>\n", p);
		if (!strcmp(p, "Child said <Ready>"))
			Resume();
		else if (!strcmp(p, "OK"))
			Resume();
	}
}

static void
ex_pipe2(struct bufferevent *bev, short what, void *arg)
{
	int i, status;

	(void)arg;
	printf("%s(%p, 0x%x, %p)\n", __func__, bev, what, arg);
	bufferevent_disable(e_pipe2, EV_READ);

	i = wait4(child, &status, 0, NULL);
	printf("stopped i %d status 0x%x\n", i, status);
	child = 0;
	close(pipe1[1]);
	close(pipe2[0]);
}

/*--------------------------------------------------------------------*/

static void
cmd_start(char **av)
{

	(void)av;
	assert(pipe(pipe1) == 0);
	assert(pipe(pipe2) == 0);

	e_pipe2 = bufferevent_new(pipe2[0], rd_pipe2, NULL, ex_pipe2, NULL);
	assert(e_pipe2 != NULL);
	bufferevent_base_set(eb, e_pipe2);
	bufferevent_enable(e_pipe2, EV_READ);

	child = fork();
	if (!child) {
		dup2(pipe1[0], 0);
		dup2(pipe2[1], 1);
		dup2(pipe2[1], 2);
		close(pipe1[0]);
		close(pipe1[1]);
		close(pipe2[0]);
		close(pipe2[1]);
		write(2, "Forked\n", 7);
		assert(chdir("../varnishd") == 0);
		execl(
		    "./varnishd",
		    "varnishd",
		    "-b", "localhost 8081",
		    "-sfile,/tmp/,10m",
		    NULL);
		perror("execl");
		exit (2);
	}
	close(pipe1[0]);
	close(pipe2[1]);
	Pause();
}


/*--------------------------------------------------------------------*/

static void
cmd_stop(char **av)
{

	(void)av;
	if (child == 0) {
		fprintf(stderr, "No child running\n");
		exit (2);
	}
	cli_write("exit\n");
	/* XXX: arm timeout */
}

/*--------------------------------------------------------------------*/

static void
cmd_cli(char **av)
{

	if (child == 0) {
		fprintf(stderr, "No child running\n");
		exit (2);
	}
	cli_write(av[0]);
	cli_write("\n");
	Pause();
}

/*--------------------------------------------------------------------*/

static void
cmd_vcl(char **av)
{
	char *p, buf[5];

	if (child == 0) {
		fprintf(stderr, "No child running\n");
		exit (2);
	}
	if (av[0] == NULL || av[1] == NULL) {
		fprintf(stderr, "usage: vcl $name $vcl\n");
		exit (2);
	}
	cli_write("config.inline ");
	cli_write(av[0]);
	cli_write(" \"");

	/* Always insert our magic backend first */

	cli_write(
	    "backend default {\\n"
	    "    set backend.host = \\\"localhost\\\";\\n"
	    "    set backend.port = \\\"8081\\\";\\n"
	    "}\\n");

	for (p = av[1]; *p; p++) {
		if (*p < ' ' || *p == '"' || *p == '\\' || *p > '~') {
			sprintf(buf, "\\%03o", *p);
			cli_write(buf);
		} else {
			write(pipe1[1], p, 1);
		}
	}
	cli_write("\"\n");
	Pause();
}

/*--------------------------------------------------------------------*/

static int req_sock = -1;
static struct bufferevent *e_req;

static void
req_write(const char *s)
{

	write(req_sock, s, strlen(s));
}

/*--------------------------------------------------------------------*/
static void
rd_req(struct bufferevent *bev, void *arg)
{
	char *p;

	(void)arg;
	while (1) {
		p = evbuffer_readline(bev->input);
		if (p == NULL)
			return;
		printf("R: <<%s>>\n", p);
	}
}

static void
ex_req(struct bufferevent *bev, short what, void *arg)
{

	(void)arg;
	printf("%s(%p, 0x%x, %p)\n", __func__, bev, what, arg);
	bufferevent_disable(e_req, EV_READ);
	bufferevent_free(e_req);
	e_req = NULL;
	close(req_sock);
	req_sock = -1;
	Resume();
}


/*--------------------------------------------------------------------*/
static void
cmd_open(char **av)
{
	struct addrinfo ai, *r0, *r1;
	int i, j, s = -1;

	(void)av;
	memset(&ai, 0, sizeof ai);
	ai.ai_family = PF_UNSPEC;
	ai.ai_socktype = SOCK_STREAM;
	ai.ai_flags = AI_PASSIVE;
	i = getaddrinfo("localhost", "8080", &ai, &r0);

	if (i) {
		fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(i));
		return;
	}

	for (r1 = r0; r1 != NULL; r1 = r1->ai_next) {
		s = socket(r1->ai_family, r1->ai_socktype, r1->ai_protocol);
		if (s < 0)
			continue;
		j = 1;
		i = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &j, sizeof j);
		assert(i == 0);

		i = connect(s, r1->ai_addr, r1->ai_addrlen);
		if (i) {
			perror("connect");
			close(s);
			s = -1;
			continue;
		}
		assert(i == 0);
		req_sock = s;
		break;
	}
	freeaddrinfo(r0);
	if (s < 0) {
		perror("connect");
		exit (2);
	}
	e_req = bufferevent_new(s, rd_req, NULL, ex_req, NULL);
	assert(e_req != NULL);
	bufferevent_base_set(eb, e_req);
	bufferevent_enable(e_req, EV_READ);
}

static void
cmd_close(char **av)
{

	if (req_sock == -1)
		return;
	(void)av;
	bufferevent_disable(e_req, EV_READ);
	bufferevent_free(e_req);
	e_req = NULL;
	close(req_sock);
	req_sock = -1;
}

/*--------------------------------------------------------------------*/

static void
cmd_req(char **av)
{
	char *p = av[0];

	if (req_sock == -1)
		cmd_open(av);
	if (*p == '!') {
		req_write(p + 1);
		shutdown(req_sock, SHUT_WR);
	} else {
		req_write(p);
	}
	Pause();
}

/*--------------------------------------------------------------------*/

static void
cmd_exit(char **av)
{

	(void)av;
	cmd_close(NULL);
	cmd_stop(NULL);
	exit (0);
}

/*--------------------------------------------------------------------*/

static struct bufferevent *e_cmd;
static int run = 1;

static void
rd_cmd(struct bufferevent *bev, void *arg)
{
	char *p;
	char **av;

	(void)bev;
	(void)arg;
	while (run) {
		p = evbuffer_readline(bev->input);
		if (p == NULL)
			return;
		printf("]: <<%s>>\n", p);
		av = ParseArgv(p, 1);
		if (av[0] != NULL) {
			fprintf(stderr, "%s\n", av[0]);
			exit (1);
		}
		if (av[1] == NULL)
			continue;
		if (!strcmp(av[1], "start"))
			cmd_start(av + 2);
		else if (!strcmp(av[1], "stop"))
			cmd_stop(av + 2);
		else if (!strcmp(av[1], "serve"))
			cmd_serve(av + 2);
		else if (!strcmp(av[1], "cli"))
			cmd_cli(av + 2);
		else if (!strcmp(av[1], "vcl"))
			cmd_vcl(av + 2);
		else if (!strcmp(av[1], "open"))
			cmd_open(av + 2);
		else if (!strcmp(av[1], "close"))
			cmd_close(av + 2);
		else if (!strcmp(av[1], "req"))
			cmd_req(av + 2);
		else if (!strcmp(av[1], "exit"))
			cmd_exit(av + 2);
		else {
			fprintf(stderr, "Unknown command \"%s\"\n", av[1]);
			exit (2);
		}
		FreeArgv(av);
	}
}

static void
ex_cmd(struct bufferevent *bev, short what, void *arg)
{

	(void)arg;
	printf("%s(%p, 0x%x, %p)\n", __func__, bev, what, arg);
	bufferevent_disable(e_cmd, EV_READ);
	bufferevent_free(e_cmd);
	e_cmd = NULL;
	cmd_close(NULL);
	cmd_stop(NULL);
	exit(0);
}

static void
Pause()
{
	assert(run == 1);
	printf("X: Pause\n");
	run = 0;
	bufferevent_disable(e_cmd, EV_READ);
}

static void
Resume()
{
	assert(run == 0);
	printf("X: Resume\n");
	run = 1;
	bufferevent_enable(e_cmd, EV_READ);
	rd_cmd(e_cmd, NULL);
}

/*--------------------------------------------------------------------*/

int
main(int argc, char **argv)
{

	setbuf(stdout, NULL);
	(void)argc;
	(void)argv;

	eb = event_init();
	assert(eb != NULL);

	open_serv_sock();

	e_cmd = bufferevent_new(0, rd_cmd, NULL, ex_cmd, NULL);
	assert(e_cmd != NULL);
	bufferevent_base_set(eb, e_cmd);
	bufferevent_enable(e_cmd, EV_READ);

	event_base_loop(eb, 0);
	return (2);
}
