/* $Id$ */

MAC_STAT(client_conn,		uint64_t, "u", "Client connections accepted")
MAC_STAT(client_req,		uint64_t, "u", "Client requests received")

MAC_STAT(cache_hit,		uint64_t, "u", "Cache hits")
MAC_STAT(cache_hitpass,		uint64_t, "u", "Cache hits for pass")
MAC_STAT(cache_miss,		uint64_t, "u", "Cache misses")

MAC_STAT(backend_conn,		uint64_t, "u", "Backend connections initiated")
MAC_STAT(backend_recycle,	uint64_t, "u", "Backend connections recyles")

MAC_STAT(n_srcaddr,		uint64_t, "u", "N struct srcaddr")
MAC_STAT(n_sess,		uint64_t, "u", "N struct sess")
MAC_STAT(n_object,		uint64_t, "u", "N struct object")
MAC_STAT(n_objecthead,		uint64_t, "u", "N struct objecthead")
MAC_STAT(n_header,		uint64_t, "u", "N struct header")
MAC_STAT(n_smf,			uint64_t, "u", "N struct smf")
MAC_STAT(n_vbe,			uint64_t, "u", "N struct vbe")
MAC_STAT(n_vbe_conn,		uint64_t, "u", "N struct vbe_conn")
MAC_STAT(n_wrk,			uint64_t, "u", "N worker threads")
MAC_STAT(n_wrk_create,		uint64_t, "u", "N worker threads created")
MAC_STAT(n_wrk_failed,		uint64_t, "u", "N worker threads not created")
MAC_STAT(n_wrk_max,		uint64_t, "u", "N worker threads limited")
MAC_STAT(n_wrk_busy,		uint64_t, "u", "N busy worker threads")
MAC_STAT(n_wrk_queue,		uint64_t, "u", "N queued work requests")

MAC_STAT(losthdr,		uint64_t, "u", "HTTP header overflows")
