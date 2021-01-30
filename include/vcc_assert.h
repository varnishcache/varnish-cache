#define assert(e)							\
do {									\
	if (!(e)) {							\
		VPI_Fail(__func__, __FILE__, __LINE__, #e);		\
	}								\
} while (0)
