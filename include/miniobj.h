/*
 * Written by Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * This file is in the public domain.
 *
 */

#define ZERO_OBJ(to, sz)						\
	do {								\
		void *(*volatile z_obj)(void *, int, size_t) = memset;	\
		(void)z_obj(to, 0, sz);					\
	} while (0)

#define INIT_OBJ(to, type_magic)					\
	do {								\
		(void)memset(to, 0, sizeof *(to));			\
		(to)->magic = (type_magic);				\
	} while (0)

#define FINI_OBJ(to)							\
	do {								\
		ZERO_OBJ(&(to)->magic, sizeof (to)->magic);		\
		to = NULL;						\
	} while (0)

#define ALLOC_OBJ(to, type_magic)					\
	do {								\
		(to) = calloc(1, sizeof *(to));				\
		if ((to) != NULL)					\
			(to)->magic = (type_magic);			\
	} while (0)

#define ALLOC_OBJ_EXTRA(to, extra_size, type_magic)			\
	do {								\
		(to) = calloc(1, sizeof(*(to)) + (extra_size));		\
		if ((to) != NULL)					\
			(to)->magic = (type_magic);			\
	} while (0)

#define SIZEOF_FLEX_OBJ(to, fld, len)					\
	(offsetof(typeof(*to), fld) + sizeof *(to)->fld * len)

#define ALLOC_FLEX_OBJ(to, fld, len, type_magic)			\
	do {								\
		(to) = calloc(1, SIZEOF_FLEX_OBJ(to, fld, len));	\
		if ((to) != NULL)					\
			(to)->magic = (type_magic);			\
	} while (0)

#define FREE_OBJ(to)							\
	do {								\
		ZERO_OBJ(&(to)->magic, sizeof (to)->magic);		\
		free(to);						\
		to = NULL;						\
	} while (0)

#define VALID_OBJ(ptr, type_magic)					\
	((ptr) != NULL && (ptr)->magic == (type_magic))

#define CHECK_OBJ(ptr, type_magic)					\
	do {								\
		assert((ptr)->magic == type_magic);			\
	} while (0)

#define CHECK_OBJ_NOTNULL(ptr, type_magic)				\
	do {								\
		assert((ptr) != NULL);					\
		assert((ptr)->magic == type_magic);			\
	} while (0)

#define CHECK_OBJ_ORNULL(ptr, type_magic)				\
	do {								\
		if ((ptr) != NULL)					\
			assert((ptr)->magic == type_magic);		\
	} while (0)

#define CAST_OBJ(to, from, type_magic)					\
	do {								\
		(to) = (from);						\
		if ((to) != NULL)					\
			CHECK_OBJ((to), (type_magic));			\
	} while (0)

#define CAST_OBJ_NOTNULL(to, from, type_magic)				\
	do {								\
		(to) = (from);						\
		AN((to));						\
		CHECK_OBJ((to), (type_magic));				\
	} while (0)

#define TAKE_OBJ_NOTNULL(to, pfrom, type_magic)				\
	do {								\
		AN((pfrom));						\
		(to) = *(pfrom);					\
		*(pfrom) = NULL;					\
		CHECK_OBJ_NOTNULL((to), (type_magic));			\
	} while (0)

#define REPLACE(ptr, val)						\
	do {								\
		const char *_vreplace = (val);				\
		free(ptr);						\
		if (_vreplace != NULL) {				\
			ptr = strdup(_vreplace);			\
			AN((ptr));					\
		} else {						\
			ptr = NULL;					\
		}							\
	} while (0)
