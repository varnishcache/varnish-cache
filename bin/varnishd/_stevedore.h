/*
 * $Id: cache.h 164 2006-05-01 12:45:20Z phk $
 */

struct stevedore;

typedef void storage_init_f(struct stevedore *, const char *spec);
typedef void storage_open_f(struct stevedore *);
typedef struct storage *storage_alloc_f(struct stevedore *, size_t size);
typedef void storage_free_f(struct storage *);

struct stevedore {
	const char		*name;
	storage_init_f		*init;	/* called by mgt process */
	storage_open_f		*open;	/* called by cache process */
	storage_alloc_f		*alloc;
	storage_free_f		*free;

	/* private fields */
	void			*priv;
};
