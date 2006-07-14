/*
 * $Id$
 *
 * Define the layout of the shared memory log segment.
 *
 * NB: THIS IS NOT A PUBLIC API TO VARNISH!
 *
 */

#define SHMLOG_FILENAME		"/tmp/_.vsl"

#include <time.h>

#include "stats.h"

struct shmloghead {
#define SHMLOGHEAD_MAGIC	4185512498U	/* From /dev/random */
	unsigned		magic;

	unsigned		hdrsize;

	time_t			starttime;

	/*
	 * Byte offset into the file where the fifolog starts
 	 * This allows the header to expand later.
	 */
	unsigned		start;

	/* Length of the fifolog area in bytes */
	unsigned		size;

	/* Current write position relative to the beginning of start */
	unsigned		ptr;

	struct varnish_stats	stats;
};

/*
 * Record format is as follows:
 *
 *	1 byte		field type (enum shmlogtag)
 *	1 byte		length of contents
 *	2 byte		record identifier
 *	n bytes		field contents (isgraph(c) || isspace(c)) allowed.
 */

/*
 * The identifiers in shmlogtag are "SLT_" + XML tag.  A script may be run
 * on this file to extract the table rather than handcode it
 */
enum shmlogtag {
	SLT_ENDMARKER = 0,
#define SLTM(foo)	SLT_##foo,
#include "shmlog_tags.h"
#undef SLTM
	SLT_WRAPMARKER = 255
};
