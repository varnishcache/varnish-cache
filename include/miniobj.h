/*
 * Initial implementation by Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * $Id$
 */

#define FREE_OBJ(to)							\
	do {								\
		(to)->magic = (0);					\
		free(to);						\
	} while (0)

#define CHECK_OBJ(ptr, type_magic)					\
	do {								\
		assert((ptr)->magic == type_magic);			\
	} while (0)

#define CHECK_OBJ_NOTNULL(ptr, type_magic)				\
	do {								\
		assert((ptr) != NULL);					\
		assert((ptr)->magic == type_magic);			\
	} while (0)

#define CAST_OBJ(to, from, type_magic)					\
	do {								\
		(to) = (from);						\
		if ((to) != NULL)					\
			CHECK_OBJ((to), (type_magic));			\
	} while (0);

#define CAST_OBJ_NOTNULL(to, from, type_magic)					\
	do {								\
		(to) = (from);						\
		assert((to) != NULL);					\
		CHECK_OBJ((to), (type_magic));				\
	} while (0);

