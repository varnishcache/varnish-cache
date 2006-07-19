/* $Id$ */

#include <stdint.h>

struct varnish_stats {
	time_t			start_time;
#define MAC_STAT(n,t,f,e)	t n;
#include "stat_field.h"
#undef MAC_STAT
};
