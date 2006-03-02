/*
 * $Id$
 */

#ifndef VARNISHAPI_H_INCLUDED
#define VARNISHAPI_H_INCLUDED

#define V_DEAD __attribute__ ((noreturn))

/* varnish_debug.c */
void		 vdb_panic(const char *, ...) V_DEAD;

/* varnish_log.c */
typedef struct vlo_buffer vlo_buffer_t;
vlo_buffer_t	*vlo_open(const char *, size_t, int);
ssize_t		 vlo_write(vlo_buffer_t *, const void *, size_t);
vlo_buffer_t	*vlo_attach(const char *);
ssize_t		 vlo_read(vlo_buffer_t *, const void *, size_t);
#if 0
uuid_t		 vlo_get_uuid(vlo_buffer_t *);
#endif
int		 vlo_close(vlo_buffer_t *);

/* varnish_util.c */
int		 vut_open_lock(const char *, int, int, int);

#endif
