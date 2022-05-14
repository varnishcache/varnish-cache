#ifndef _PATHMAX_H____
#define _PATHMAX_H____

#include <limits.h>

/*
workaround for non-existent PATH_MAX on some platforms e.g. GNU HURD
*/
#ifndef PATH_MAX
#define PATH_MAX 255
#endif

#endif
