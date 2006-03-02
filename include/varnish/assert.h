/*
 * $Id$
 */

#ifndef VARNISH_ASSERT_H_INCLUDED
#define VARNISH_ASSERT_H_INCLUDED

#ifdef NDEBUG
#define V_ASSERT(test) \
	do { /* nothing */ } while (0)
#else
#define V_ASSERT(test) \
	do { \
		if (!(test)) \
			vdb_panic("assertion failed in %s line %d: %s", \
			    #test, __FILE__, __LINE__); \
	} while (0)
#endif

#endif
