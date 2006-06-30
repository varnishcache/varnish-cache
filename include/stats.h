/* $Id$ */

#include <stdint.h>

struct varnish_stats {
#define MAC_STAT(n,t,f,e)	t n;
#include "stat_field.h"
#undef MAC_STAT
};
