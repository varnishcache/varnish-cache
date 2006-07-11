/*
 * $Id: varnishlog.c 153 2006-04-25 08:17:43Z phk $
 */

#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <regex.h>
#include <sys/mman.h>

#include "shmlog.h"
#include "varnishapi.h"

struct VSL_data {
	struct shmloghead	*head;
	unsigned char		*logstart;
	unsigned char		*logend;
	unsigned char		*ptr;

	FILE			*fi;
	unsigned char		rbuf[4 + 255 + 1];

	int			b_opt;
	int			c_opt;

	int			ix_opt;
	unsigned char 		supr[256];

	unsigned char		dir[65536];

	int			regflags;
	regex_t			*regincl;
	regex_t			*regexcl;
};

#ifndef MAP_HASSEMAPHORE
#define MAP_HASSEMAPHORE 0 /* XXX Linux */
#endif

static int vsl_fd;
static struct shmloghead *vsl_lh;


/*--------------------------------------------------------------------*/

const char *VSL_tags[256] = {
#define SLTM(foo)       [SLT_##foo] = #foo,
#include "shmlog_tags.h"
#undef SLTM
};

/*--------------------------------------------------------------------*/

static int
vsl_shmem_map(void)
{
	int i;
	struct shmloghead slh;

	if (vsl_lh != NULL)
		return (0);

	vsl_fd = open(SHMLOG_FILENAME, O_RDONLY);
	if (vsl_fd < 0) {
		fprintf(stderr, "Cannot open %s: %s\n",
		    SHMLOG_FILENAME, strerror(errno));
		return (1);
	}
	i = read(vsl_fd, &slh, sizeof slh);
	if (i != sizeof slh) {
		fprintf(stderr, "Cannot read %s: %s\n",
		    SHMLOG_FILENAME, strerror(errno));
		return (1);
	}
	if (slh.magic != SHMLOGHEAD_MAGIC) {
		fprintf(stderr, "Wrong magic number in file %s\n",
		    SHMLOG_FILENAME);
		return (1);
	}

	vsl_lh = mmap(NULL, slh.size + sizeof slh,
	    PROT_READ, MAP_HASSEMAPHORE, vsl_fd, 0);
	if (vsl_lh == MAP_FAILED) {
		fprintf(stderr, "Cannot mmap %s: %s\n",
		    SHMLOG_FILENAME, strerror(errno));
		return (1);
	}
	return (0);
}

/*--------------------------------------------------------------------*/

struct VSL_data *
VSL_New(void)
{
	struct VSL_data *vd;

	vd = calloc(sizeof *vd, 1);
	vd->regflags = REG_EXTENDED | REG_NOSUB;
	return (vd);
}

int
VSL_OpenLog(struct VSL_data *vd)
{


	if (vd->fi != NULL)
		return (0);

	if (vsl_shmem_map())
		return (1);

	vd->head = vsl_lh;
	vd->logstart = (unsigned char *)vsl_lh + vsl_lh->start;
	vd->logend = vd->logstart + vsl_lh->size;
	vd->ptr = vd->logstart;
	return (0);
}

/*--------------------------------------------------------------------*/

static unsigned char *
vsl_nextlog(struct VSL_data *vd)
{
	unsigned char *p;
	int i;

	if (vd->fi != NULL) {
		i = fread(vd->rbuf, 4, 1, vd->fi);
		if (i != 1)
			return (NULL);
		if (vd->rbuf[1] > 0) {
			i = fread(vd->rbuf + 4, vd->rbuf[1], 1, vd->fi);
			if (i != 1)
				return (NULL);
		}
		return (vd->rbuf);
	}

	p = vd->ptr;
	while (1) {
		if (*p == SLT_WRAPMARKER) {
			p = vd->logstart;
			continue;
		}
		if (*p == SLT_ENDMARKER) {
			vd->ptr = p;
			return (NULL);
		}
		vd->ptr = p + p[1] + 4;
		return (p);
	}
}

unsigned char *
VSL_NextLog(struct VSL_data *vd)
{
	unsigned char *p;
	regmatch_t rm;
	unsigned u;
	int i;

	while (1) {
		p = vsl_nextlog(vd);
		if (p == NULL)
			return (p);
		u = (p[2] << 8) | p[3];
		switch(p[0]) {
		case SLT_SessionOpen:
			vd->dir[u] = 1;
			break;
		case SLT_BackendOpen:
			vd->dir[u] = 2;
			break;
		default:
			break;
		}
		if (vd->supr[p[0]]) 
			continue;
		if (vd->b_opt && vd->dir[u] == 1)
			continue;
		if (vd->c_opt && vd->dir[u] == 2)
			continue;
		if (vd->regincl != NULL) {
			rm.rm_so = 0;
			rm.rm_eo = p[1];
			i = regexec(vd->regincl, p + 4, 1, &rm, REG_STARTEND);
			if (i == REG_NOMATCH)
				continue;
		}
		if (vd->regexcl != NULL) {
			rm.rm_so = 0;
			rm.rm_eo = p[1];
			i = regexec(vd->regexcl, p + 4, 1, &rm, REG_STARTEND);
			if (i != REG_NOMATCH)
				continue;
		}
		return (p);
	}
}

/*--------------------------------------------------------------------*/

static int
vsl_r_arg(struct VSL_data *vd, const char *opt)
{

	if (!strcmp(opt, "-"))
		vd->fi = stdin;
	else
		vd->fi = fopen(opt, "r");
	if (vd->fi != NULL)
		return (1);
	perror(opt);
	return (-1);
}

/*--------------------------------------------------------------------*/

static int
vsl_IX_arg(struct VSL_data *vd, const char *opt, int arg)
{
	int i;
	regex_t **rp;
	char buf[BUFSIZ];

	if (arg == 'I')
		rp = &vd->regincl;
	else
		rp = &vd->regexcl;
	if (*rp != NULL) {
		fprintf(stderr, "Option %c can only be given once", arg);
		return (-1);
	}
	*rp = calloc(sizeof(regex_t), 1);
	if (*rp == NULL) {
		perror("malloc");
		return (-1);
	}
	i = regcomp(*rp, opt, vd->regflags);
	if (i) {
		regerror(i, *rp, buf, sizeof buf);
		fprintf(stderr, "%s", buf);
		return (-1);
	}
	return (1);
}

/*--------------------------------------------------------------------*/

static int
vsl_ix_arg(struct VSL_data *vd, const char *opt, int arg)
{
	int i, j, l;
	const char *b, *e, *p, *q;

	/* If first option is 'i', set all bits for supression */
	if (arg == 'i' && vd->ix_opt == 0)
		for (i = 0; i < 256; i++)
			vd->supr[i] = 1;
	vd->ix_opt = 1;

	for (b = opt; *b; b = e) {
		while (isspace(*b))
			b++;
		e = strchr(b, ',');
		if (e == NULL)
			e = strchr(b, '\0');
		l = e - b;
		if (*e == ',')
			e++;
		while (isspace(b[l - 1]))
			l--;
		for (i = 0; i < 256; i++) {
			if (VSL_tags[i] == NULL)
				continue;
			p = VSL_tags[i];
			q = b;
			for (j = 0; j < l; j++)
				if (tolower(*q++) != tolower(*p++))
					break;
			if (j != l)
				continue;

			if (arg == 'x')
				vd->supr[i] = 1;
			else
				vd->supr[i] = 0;
			break;
		}
		if (i == 256) {
			fprintf(stderr,
			    "Could not match \"%*.*s\" to any tag\n", l, l, b);
			return (-1);
		}
	}
	return (1);
}

/*--------------------------------------------------------------------*/

int
VSL_Arg(struct VSL_data *vd, int arg, const char *opt)
{
	switch (arg) {
	case 'b': vd->b_opt = !vd->b_opt; return (1);
	case 'c': vd->c_opt = !vd->c_opt; return (1);
	case 'i': case 'x': return (vsl_ix_arg(vd, opt, arg));
	case 'r': return (vsl_r_arg(vd, opt));
	case 'I': case 'X': return (vsl_IX_arg(vd, opt, arg));
	case 'C': vd->regflags = REG_ICASE; return (1);
	default:
		return (0);
	}
}

struct varnish_stats *
VSL_OpenStats(void)
{

	if (vsl_shmem_map())
		return (NULL);
	return (&vsl_lh->stats);
}

