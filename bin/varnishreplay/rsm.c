#include "rsm.h"

#include <sys/types.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#include "vas.h"

struct replay_shm* RSM_head;

void
rsm_atexit(void)
{
}

void
rsm_init(const char* app)
{
	int i, fill;
	uintmax_t size;

	char* fn = NULL;
	if (VIN_N_Arg(VSM_DIRNAME, NULL, NULL, &fn)) {
		fprintf(stderr, "Could not create rsm file name: %s\n",
		    strerror(errno));
		exit (1);
	}

	size = 10 * 1024 * 1024;
	fill = 0;

	i = open(fn, O_RDWR, 0644);
	if (i >= 0) {
		(void)close(i);
	}
	(void)unlink(fn);

	int vsl_fd = open(fn, O_RDWR | O_CREAT | O_EXCL | O_NONBLOCK, 0644);
	if (vsl_fd < 0) {
		fprintf(stderr, "Could not create %s: %s\n",
		    fn, strerror(errno));
		exit (1);
	}
	AZ(ftruncate(vsl_fd, (off_t)size));

	RSM_head = (struct replay_shm *)mmap(NULL, size,
	    PROT_READ|PROT_WRITE,
	    MAP_HASSEMAPHORE | MAP_NOSYNC | MAP_SHARED,
	    vsl_fd, 0);
	memset((void *)RSM_head, fill, sizeof(*RSM_head));
	AZ(atexit(rsm_atexit));
	xxxassert(RSM_head != MAP_FAILED);

	if (app) strcpy(RSM_head->rsm_gen.appname, app);
}

static int vsm_fd;
static char* fname;
static struct stat fdstat;

int
rsm_open()
{
	if (VIN_N_Arg(VSM_DIRNAME, NULL, NULL, &fname)) {
		fprintf(stderr, "Could not create rsm file name: %s\n",
		    strerror(errno));
		return (1);
	}

	vsm_fd = open(fname, O_RDONLY);
	if (vsm_fd < 0) {
		fprintf(stderr, "Cannot open %s: %s\n",
		    fname, strerror(errno));
		return (1);
	}

	assert(fstat(vsm_fd, &fdstat) == 0);
	if (!S_ISREG(fdstat.st_mode)) {
		AZ(close(vsm_fd));
		vsm_fd = -1;
		return (1);
	}

	RSM_head = (void *)mmap(NULL, sizeof(*RSM_head),
	    PROT_READ, MAP_SHARED|MAP_HASSEMAPHORE, vsm_fd, 0);
	if (RSM_head == MAP_FAILED) {
		fprintf(stderr, "Cannot mmap %s: %s\n",
		    fname, strerror(errno));
		AZ(close(vsm_fd));
		vsm_fd = -1;
		RSM_head = NULL;
		return (1);
	}

	return (0);
}

void
rsm_close()
{
	if (RSM_head == NULL)
		return;
	assert(0 == munmap((void*)RSM_head, sizeof(*RSM_head)));
	RSM_head = NULL;
	assert(vsm_fd >= 0);
	assert(0 == close(vsm_fd));
	vsm_fd = -1;
}

int
rsm_reopen()
{
	struct stat st;
	int i;

	AN(RSM_head);

	if (stat(fname, &st))
		return (0);

	if (st.st_dev == fdstat.st_dev && st.st_ino == fdstat.st_ino)
		return (0);

	rsm_close();
	for (i = 0; i < 5; i++) {
		if (!rsm_open())
			return (1);
	}
	if (rsm_open())
		return (-1);

	return (1);
}