/*
 * $Id$
 */

#include "config.h"

#include <sys/file.h>
#include <sys/mman.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <varnish/assert.h>

#include <varnishapi.h>

#define VLO_MAGIC 0x564c4f00

typedef struct vlo_control vlo_control_t;

/* should this be part of the exported API? */
struct vlo_control {
	uint32_t	 magic;
#if 0
	uuid_t		 uuid;
#endif
	uint32_t	 size;
	uint32_t	 head;
	uint32_t	 tail;
};

struct vlo_buffer {
	int		 mode;
	int		 cfd;
	vlo_control_t	*ctl;
	int		 bfd;
	unsigned char	*buf;
	uint32_t	 rpos;
};

/*
 * Open a log file for writing; create it if necessary.  If the control
 * file already exists, try to preserve its state, in case someone is
 * already listening.
 */
vlo_buffer_t *
vlo_open(const char *name, size_t size, int perm)
{
	char ctlname[PATH_MAX];
	vlo_buffer_t *vb;
	int page_size;
	int i, serr;

	page_size = getpagesize();

	V_ASSERT(size > 0);
	V_ASSERT(size % page_size == 0);

	if (snprintf(ctlname, sizeof ctlname, "%s.ctl", name) >= sizeof ctlname) {
		errno = ENAMETOOLONG;
		return (NULL);
	}
	if ((vb = malloc(sizeof *vb)) == NULL)
		goto out;
	vb->mode = O_RDWR;
	vb->cfd = -1;
	vb->ctl = NULL;
	vb->bfd = -1;
	vb->buf = NULL;
	vb->rpos = 0;

	/* open, lock and mmap the control file */
	if ((vb->cfd = vut_open_lock(ctlname, O_RDWR|O_CREAT,
		 LOCK_EX|LOCK_NB, perm)) == -1 ||
	    ftruncate(vb->cfd, page_size) == -1 ||
	    (vb->ctl = mmap(NULL, page_size, PROT_READ|PROT_WRITE,
		MAP_SHARED, vb->cfd, 0)) == NULL ||
	    mlock(vb->ctl, page_size) == -1)
		goto out;

	/* open, lock and mmap the buffer file */
	if ((vb->bfd = open(name, O_RDWR|O_CREAT, perm)) == -1 ||
	    flock(vb->bfd, LOCK_EX) == -1 ||
	    ftruncate(vb->bfd, size) == -1 ||
	    (vb->buf = mmap(NULL, size, PROT_READ|PROT_WRITE,
		MAP_SHARED, vb->bfd, 0)) == NULL ||
	    mlock(vb->ctl, size) == -1)
		goto out;

	/* initialize control structures */
	if (vb->ctl->magic != VLO_MAGIC ||
	    vb->ctl->size != size ||
	    vb->ctl->head >= size ||
	    vb->ctl->tail >= size) {
		vb->ctl->magic = VLO_MAGIC;
#if 0
		vb->ctl->uuid = /* XXX */;
#endif
		vb->ctl->size = size;
		vb->ctl->head = size - page_size; /* early wraparound */
		vb->ctl->tail = vb->ctl->head;
		vb->rpos = vb->ctl->tail;
	}

	/* pre-fault buffer */
	for (i = 0; i < size; i += page_size)
		vb->buf[i] = '\0';

	return (vb);
 out:
	serr = errno;
	if (vb != NULL) {
		if (vb->buf != NULL) {
			munlock(vb->buf, size);
			munmap(vb->buf, size);
		}
		if (vb->bfd != -1)
			close(vb->bfd);
		if (vb->ctl != NULL) {
			munlock(vb->ctl, page_size);
			munmap(vb->ctl, page_size);
		}
		if (vb->cfd != -1)
			close(vb->cfd);
		free(vb);
	}
	errno = serr;
	return (NULL);
}

/*
 * Write to a log file.
 */
ssize_t
vlo_write(vlo_buffer_t *vb, const void *data, size_t len)
{
	ssize_t result;
	size_t copylen;

	V_ASSERT(vb != NULL);
	V_ASSERT(vb->mode == O_WRONLY || vb->mode == O_RDWR);
	V_ASSERT(vb->cfd != -1 && vb->ctl != NULL);
	V_ASSERT(vb->bfd != -1 && vb->buf != NULL);
	V_ASSERT(vb->ctl->magic == VLO_MAGIC);

	for (result = 0; len > 0; len -= copylen, result += copylen) {
		if (vb->ctl->head + len > vb->ctl->size)
			copylen = vb->ctl->size - vb->ctl->head;
		else
			copylen = len;
		if (vb->ctl->tail > vb->ctl->head &&
		    vb->ctl->tail <= vb->ctl->head + copylen)
			vb->ctl->tail =
			    (vb->ctl->head + copylen + 1) % vb->ctl->size;
		memcpy(vb->buf + vb->ctl->head, data, copylen);
		vb->ctl->head = (vb->ctl->head + copylen) % vb->ctl->size;
	}
	return (result);
}

/*
 * Attach to an existing log buffer.
 */
vlo_buffer_t *
vlo_attach(const char *name)
{
	char ctlname[PATH_MAX];
	vlo_buffer_t *vb;
	int page_size;
	int serr;

	page_size = getpagesize();

	if (snprintf(ctlname, sizeof ctlname, "%s.ctl", name) >= sizeof ctlname) {
		errno = ENAMETOOLONG;
		return (NULL);
	}
	if ((vb = malloc(sizeof *vb)) == NULL)
		goto out;
	vb->mode = O_RDONLY;
	vb->cfd = -1;
	vb->ctl = NULL;
	vb->bfd = -1;
	vb->buf = NULL;
	vb->rpos = 0;

	/* open, lock and mmap the control file */
	if ((vb->cfd = open(ctlname, O_RDONLY)) == -1 ||
	    (vb->ctl = mmap(NULL, page_size, PROT_READ,
		MAP_SHARED, vb->cfd, 0)) == NULL ||
	    mlock(vb->ctl, page_size) == -1)
		goto out;

	/* verify control structure */
	if (vb->ctl->magic != VLO_MAGIC ||
	    !(vb->ctl->size > 0 && (vb->ctl->size % page_size) == 0)) {
		errno = EINVAL; /* XXX document */
		goto out;
	}

	/* open, lock and mmap the buffer file */
	if ((vb->bfd = open(name, O_RDONLY)) == -1 ||
	    (vb->buf = mmap(NULL, vb->ctl->size, PROT_READ,
		MAP_SHARED, vb->bfd, 0)) == NULL ||
	    mlock(vb->ctl, vb->ctl->size) == -1)
		goto out;

	vb->rpos = vb->ctl->tail;

	return (vb);
 out:
	serr = errno;
	if (vb != NULL) {
		if (vb->buf != NULL) {
			munlock(vb->buf, vb->ctl->size);
			munmap(vb->buf, vb->ctl->size);
		}
		if (vb->bfd != -1)
			close(vb->bfd);
		if (vb->ctl != NULL) {
			munlock(vb->ctl, page_size);
			munmap(vb->ctl, page_size);
		}
		if (vb->cfd != -1)
			close(vb->cfd);
		free(vb);
	}
	errno = serr;
	return (NULL);
}

/*
 * Read from a log file.
 */
ssize_t
vlo_read(vlo_buffer_t *vb, const void *data, size_t len)
{
	V_ASSERT(vb != NULL);
	V_ASSERT(vb->mode == O_RDONLY || vb->mode == O_RDWR);
	V_ASSERT(vb->cfd != -1 && vb->ctl != NULL);
	V_ASSERT(vb->bfd != -1 && vb->buf != NULL);
	V_ASSERT(vb->ctl->magic == VLO_MAGIC);

	/* not implemented */
	return (-1);
}

#if 0
/*
 * Return the UUID of the process writing to the log file.
 */
uuid_t
vlo_get_uuid(vlo_buffer *vb)
{
	V_ASSERT(vb != NULL);
	V_ASSERT(vb->cfd != -1 && vb->ctl != NULL);
	V_ASSERT(vb->bfd != -1 && vb->buf != NULL);
	V_ASSERT(vb->ctl->magic == VLO_MAGIC);

	return (vb->ctl->uuid);
}
#endif

/*
 * Close a log file.
 */
int
vlo_close(vlo_buffer_t *vb)
{
	int page_size;

	page_size = getpagesize();

	V_ASSERT(vb != NULL);
	V_ASSERT(vb->cfd != -1 && vb->ctl != NULL);
	V_ASSERT(vb->bfd != -1 && vb->buf != NULL);
	V_ASSERT(vb->ctl->magic == VLO_MAGIC);

	munlock(vb->buf, vb->ctl->size);
	munmap(vb->buf, vb->ctl->size);
	close(vb->bfd);
	munlock(vb->ctl, page_size);
	munmap(vb->ctl, page_size);
	close(vb->cfd);
	free(vb);
	return (0);
}
