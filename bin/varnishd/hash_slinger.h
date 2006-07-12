/*
 * $Id$
 */

typedef int hash_init_f(const char *);
typedef void hash_start_f(void);
typedef struct objhead *hash_lookup_f(const char *key1, const char *key2, struct objhead *nobj);
typedef int hash_deref_f(struct objhead *obj);

struct hash_slinger {
	const char		*name;
	hash_init_f		*init;
	hash_start_f		*start;
	hash_lookup_f		*lookup;
	hash_deref_f		*deref;
};
