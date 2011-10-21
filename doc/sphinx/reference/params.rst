acceptor_sleep_decay
	- Default: 0.900
	- Flags: experimental

	If we run out of resources, such as file descriptors or worker threads, the acceptor will sleep between accepts.
	This parameter (multiplicatively) reduce the sleep duration for each succesfull accept. (ie: 0.9 = reduce by 10%)

acceptor_sleep_incr
	- Units: s
	- Default: 0.001
	- Flags: experimental

	If we run out of resources, such as file descriptors or worker threads, the acceptor will sleep between accepts.
	This parameter control how much longer we sleep, each time we fail to accept a new connection.

acceptor_sleep_max
	- Units: s
	- Default: 0.050
	- Flags: experimental

	If we run out of resources, such as file descriptors or worker threads, the acceptor will sleep between accepts.
	This parameter limits how long it can sleep between attempts to accept new connections.

auto_restart
	- Units: bool
	- Default: on

	Restart child process automatically if it dies.

ban_dups
	- Units: bool
	- Default: on

	Detect and eliminate duplicate bans.

ban_lurker_sleep
	- Units: s
	- Default: 0.01

	How long time does the ban lurker thread sleeps between successful attempts to push the last item up the ban  list.  It always sleeps a second when nothing can be done.
	A value of zero disables the ban lurker.

between_bytes_timeout
	- Units: s
	- Default: 60

	Default timeout between bytes when receiving data from backend. We only wait for this many seconds between bytes before giving up. A value of 0 means it will never time out. VCL can override this default value for each backend request and backend request. This parameter does not apply to pipe.

cc_command
	- Default: exec gcc -std=gnu99  -pthread -fpic -shared -Wl,-x -o %o %s
	- Flags: must_reload

	Command used for compiling the C source code to a dlopen(3) loadable object.  Any occurrence of %s in the string will be replaced with the source file name, and %o will be replaced with the output file name.

cli_buffer
	- Units: bytes
	- Default: 8192

	Size of buffer for CLI input.
	You may need to increase this if you have big VCL files and use the vcl.inline CLI command.
	NB: Must be specified with -p to have effect.

cli_timeout
	- Units: seconds
	- Default: 10

	Timeout for the childs replies to CLI requests from the master.

clock_skew
	- Units: s
	- Default: 10

	How much clockskew we are willing to accept between the backend and our own clock.

connect_timeout
	- Units: s
	- Default: 0.7

	Default connection timeout for backend connections. We only try to connect to the backend for this many seconds before giving up. VCL can override this default value for each backend and backend request.

critbit_cooloff
	- Units: s
	- Default: 180.0
	- Flags: 

	How long time the critbit hasher keeps deleted objheads on the cooloff list.

default_grace
	- Units: seconds
	- Default: 10
	- Flags: delayed

	Default grace period.  We will deliver an object this long after it has expired, provided another thread is attempting to get a new copy.
	Objects already cached will not be affected by changes made until they are fetched from the backend again.

default_keep
	- Units: seconds
	- Default: 0
	- Flags: delayed

	Default keep period.  We will keep a useless object around this long, making it available for conditional backend fetches.  That means that the object will be removed from the cache at the end of ttl+grace+keep.

default_ttl
	- Units: seconds
	- Default: 120

	The TTL assigned to objects if neither the backend nor the VCL code assigns one.
	Objects already cached will not be affected by changes made until they are fetched from the backend again.
	To force an immediate effect at the expense of a total flush of the cache use "ban.url ."

diag_bitmap
	- Units: bitmap
	- Default: 0

	Bitmap controlling diagnostics code::

	  0x00000001 - CNT_Session states.
	  0x00000002 - workspace debugging.
	  0x00000004 - kqueue debugging.
	  0x00000008 - mutex logging.
	  0x00000010 - mutex contests.
	  0x00000020 - waiting list.
	  0x00000040 - object workspace.
	  0x00001000 - do not core-dump child process.
	  0x00002000 - only short panic message.
	  0x00004000 - panic to stderr.
	  0x00010000 - synchronize shmlog.
	  0x00020000 - synchronous start of persistence.
	  0x00040000 - release VCL early.
	  0x80000000 - do edge-detection on digest.
	Use 0x notation and do the bitor in your head :-)

esi_syntax
	- Units: bitmap
	- Default: 0

	Bitmap controlling ESI parsing code::

	  0x00000001 - Don't check if it looks like XML
	  0x00000002 - Ignore non-esi elements
	  0x00000004 - Emit parsing debug records
	  0x00000008 - Force-split parser input (debugging)
	Use 0x notation and do the bitor in your head :-)

expiry_sleep
	- Units: seconds
	- Default: 1

	How long the expiry thread sleeps when there is nothing for it to do.

fetch_chunksize
	- Units: kilobytes
	- Default: 128
	- Flags: experimental

	The default chunksize used by fetcher. This should be bigger than the majority of objects with short TTLs.
	Internal limits in the storage_file module makes increases above 128kb a dubious idea.

fetch_maxchunksize
	- Units: kilobytes
	- Default: 262144
	- Flags: experimental

	The maximum chunksize we attempt to allocate from storage. Making this too large may cause delays and storage fragmentation.

first_byte_timeout
	- Units: s
	- Default: 60

	Default timeout for receiving first byte from backend. We only wait for this many seconds for the first byte before giving up. A value of 0 means it will never time out. VCL can override this default value for each backend and backend request. This parameter does not apply to pipe.

group
	- Default: magic
	- Flags: must_restart

	The unprivileged group to run as.

gzip_level
	- Default: 6

	Gzip compression level: 0=debug, 1=fast, 9=best

gzip_memlevel
	- Default: 8

	Gzip memory level 1=slow/least, 9=fast/most compression.
	Memory impact is 1=1k, 2=2k, ... 9=256k.

gzip_stack_buffer
	- Units: Bytes
	- Default: 32768
	- Flags: experimental

	Size of stack buffer used for gzip processing.
	The stack buffers are used for in-transit data, for instance gunzip'ed data being sent to a client.Making this space to small results in more overhead, writes to sockets etc, making it too big is probably just a waste of memory.

gzip_tmp_space
	- Default: 0
	- Flags: experimental

	Where temporary space for gzip/gunzip is allocated::

	  0 - malloc
	  1 - session workspace
	  2 - thread workspace
	If you have much gzip/gunzip activity, it may be an advantage to use workspace for these allocations to reduce malloc activity.  Be aware that gzip needs 256+KB and gunzip needs 32+KB of workspace (64+KB if ESI processing).

gzip_window
	- Default: 15

	Gzip window size 8=least, 15=most compression.
	Memory impact is 8=1k, 9=2k, ... 15=128k.

http_gzip_support
	- Units: bool
	- Default: on
	- Flags: experimental

	Enable gzip support. When enabled Varnish will compress uncompressed objects before they are stored in the cache. If a client does not support gzip encoding Varnish will uncompress compressed objects on demand. Varnish will also rewrite the Accept-Encoding header of clients indicating support for gzip to::

	Accept-Encoding: gzip

	Clients that do not support gzip will have their Accept-Encoding header removed. For more information on how gzip is implemented please see the chapter on gzip in the Varnish reference.

http_max_hdr
	- Units: header lines
	- Default: 64

	Maximum number of HTTP headers we will deal with in client request or backend reponses.  Note that the first line occupies five header fields.
	This paramter does not influence storage consumption, objects allocate exact space for the headers they store.

http_range_support
	- Units: bool
	- Default: on
	- Flags: experimental

	Enable support for HTTP Range headers.

http_req_hdr_len
	- Units: bytes
	- Default: 8192

	Maximum length of any HTTP client request header we will allow.  The limit is inclusive its continuation lines.

http_req_size
	- Units: bytes
	- Default: 32768

	Maximum number of bytes of HTTP client request we will deal with.  This is a limit on all bytes up to the double blank line which ends the HTTP request.
	The memory for the request is allocated from the session workspace (param: sess_workspace) and this parameter limits how much of that the request is allowed to take up.

http_resp_hdr_len
	- Units: bytes
	- Default: 8192

	Maximum length of any HTTP backend response header we will allow.  The limit is inclusive its continuation lines.

http_resp_size
	- Units: bytes
	- Default: 32768

	Maximum number of bytes of HTTP backend resonse we will deal with.  This is a limit on all bytes up to the double blank line which ends the HTTP request.
	The memory for the request is allocated from the worker workspace (param: sess_workspace) and this parameter limits how much of that the request is allowed to take up.

listen_address
	- Default: :80
	- Flags: must_restart

	Whitespace separated list of network endpoints where Varnish will accept requests.
	Possible formats: host, host:port, :port

listen_depth
	- Units: connections
	- Default: 1024
	- Flags: must_restart

	Listen queue depth.

log_hashstring
	- Units: bool
	- Default: on

	Log the hash string components to shared memory log.

log_local_address
	- Units: bool
	- Default: off

	Log the local address on the TCP connection in the SessionOpen shared memory record.

lru_interval
	- Units: seconds
	- Default: 2
	- Flags: experimental

	Grace period before object moves on LRU list.
	Objects are only moved to the front of the LRU list if they have not been moved there already inside this timeout period.  This reduces the amount of lock operations necessary for LRU list access.

max_esi_depth
	- Units: levels
	- Default: 5

	Maximum depth of esi:include processing.

max_restarts
	- Units: restarts
	- Default: 4

	Upper limit on how many times a request can restart.
	Be aware that restarts are likely to cause a hit against the backend, so don't increase thoughtlessly.

nuke_limit
	- Units: allocations
	- Default: 50
	- Flags: experimental

	Maximum number of objects we attempt to nuke in orderto make space for a object body.

ping_interval
	- Units: seconds
	- Default: 3
	- Flags: must_restart

	Interval between pings from parent to child.
	Zero will disable pinging entirely, which makes it possible to attach a debugger to the child.

pipe_timeout
	- Units: seconds
	- Default: 60

	Idle timeout for PIPE sessions. If nothing have been received in either direction for this many seconds, the session is closed.

prefer_ipv6
	- Units: bool
	- Default: off

	Prefer IPv6 address when connecting to backends which have both IPv4 and IPv6 addresses.

queue_max
	- Units: %
	- Default: 100
	- Flags: experimental

	Percentage permitted queue length.

	This sets the ratio of queued requests to worker threads, above which sessions will be dropped instead of queued.

rush_exponent
	- Units: requests per request
	- Default: 3
	- Flags: experimental

	How many parked request we start for each completed request on the object.
	NB: Even with the implict delay of delivery, this parameter controls an exponential increase in number of worker threads.

saintmode_threshold
	- Units: objects
	- Default: 10
	- Flags: experimental

	The maximum number of objects held off by saint mode before no further will be made to the backend until one times out.  A value of 0 disables saintmode.

send_timeout
	- Units: seconds
	- Default: 60
	- Flags: delayed

	Send timeout for client connections. If the HTTP response hasn't been transmitted in this many
	seconds the session is closed. 
	See setsockopt(2) under SO_SNDTIMEO for more information.

sess_timeout
	- Units: seconds
	- Default: 5

	Idle timeout for persistent sessions. If a HTTP request has not been received in this many seconds, the session is closed.

sess_workspace
	- Units: bytes
	- Default: 65536
	- Flags: delayed

	Bytes of HTTP protocol workspace allocated for sessions. This space must be big enough for the entire HTTP protocol header and any edits done to it in the VCL code.
	Minimum is 1024 bytes.

session_linger
	- Units: ms
	- Default: 50
	- Flags: experimental

	How long time the workerthread lingers on the session to see if a new request appears right away.
	If sessions are reused, as much as half of all reuses happen within the first 100 msec of the previous request completing.
	Setting this too high results in worker threads not doing anything for their keep, setting it too low just means that more sessions take a detour around the waiter.

session_max
	- Units: sessions
	- Default: 100000

	Maximum number of sessions we will allocate before just dropping connections.
	This is mostly an anti-DoS measure, and setting it plenty high should not hurt, as long as you have the memory for it.

shm_reclen
	- Units: bytes
	- Default: 255

	Maximum number of bytes in SHM log record.
	Maximum is 65535 bytes.

shm_workspace
	- Units: bytes
	- Default: 8192
	- Flags: delayed

	Bytes of shmlog workspace allocated for worker threads. If too big, it wastes some ram, if too small it causes needless flushes of the SHM workspace.
	These flushes show up in stats as "SHM flushes due to overflow".
	Minimum is 4096 bytes.

shortlived
	- Units: s
	- Default: 10.0

	Objects created with TTL shorter than this are always put in transient storage.

syslog_cli_traffic
	- Units: bool
	- Default: on

	Log all CLI traffic to syslog(LOG_INFO).

thread_pool_add_delay
	- Units: milliseconds
	- Default: 2

	Wait at least this long between creating threads.

	Setting this too long results in insuffient worker threads.

	Setting this too short increases the risk of worker thread pile-up.

thread_pool_add_threshold
	- Units: requests
	- Default: 2
	- Flags: experimental

	Overflow threshold for worker thread creation.

	Setting this too low, will result in excess worker threads, which is generally a bad idea.

	Setting it too high results in insuffient worker threads.

thread_pool_fail_delay
	- Units: milliseconds
	- Default: 200
	- Flags: experimental

	Wait at least this long after a failed thread creation before trying to create another thread.

	Failure to create a worker thread is often a sign that  the end is near, because the process is running out of RAM resources for thread stacks.
	This delay tries to not rush it on needlessly.

	If thread creation failures are a problem, check that thread_pool_max is not too high.

	It may also help to increase thread_pool_timeout and thread_pool_min, to reduce the rate at which treads are destroyed and later recreated.

thread_pool_max
	- Units: threads
	- Default: 500
	- Flags: delayed, experimental

	The maximum number of worker threads in each pool.

	Do not set this higher than you have to, since excess worker threads soak up RAM and CPU and generally just get in the way of getting work done.

thread_pool_min
	- Units: threads
	- Default: 5
	- Flags: delayed, experimental

	The minimum number of worker threads in each pool.

	Increasing this may help ramp up faster from low load situations where threads have expired.

	Minimum is 2 threads.

thread_pool_purge_delay
	- Units: milliseconds
	- Default: 1000
	- Flags: delayed, experimental

	Wait this long between purging threads.

	This controls the decay of thread pools when idle(-ish).

	Minimum is 100 milliseconds.

thread_pool_stack
	- Units: bytes
	- Default: -1
	- Flags: experimental

	Worker thread stack size.
	On 32bit systems you may need to tweak this down to fit many threads into the limited address space.

thread_pool_timeout
	- Units: seconds
	- Default: 300
	- Flags: delayed, experimental

	Thread idle threshold.

	Threads in excess of thread_pool_min, which have been idle for at least this long are candidates for purging.

	Minimum is 1 second.

thread_pool_workspace
	- Units: bytes
	- Default: 65536
	- Flags: delayed

	Bytes of HTTP protocol workspace allocated for worker threads. This space must be big enough for the backend request and responses, and response to the client plus any other memory needs in the VCL code.Minimum is 1024 bytes.

thread_pools
	- Units: pools
	- Default: 2
	- Flags: delayed, experimental

	Number of worker thread pools.

	Increasing number of worker pools decreases lock contention.

	Too many pools waste CPU and RAM resources, and more than one pool for each CPU is probably detrimal to performance.

	Can be increased on the fly, but decreases require a restart to take effect.

thread_stats_rate
	- Units: requests
	- Default: 10
	- Flags: experimental

	Worker threads accumulate statistics, and dump these into the global stats counters if the lock is free when they finish a request.
	This parameters defines the maximum number of requests a worker thread may handle, before it is forced to dump its accumulated stats into the global counters.

user
	- Default: magic
	- Flags: must_restart

	The unprivileged user to run as.  Setting this will also set "group" to the specified user's primary group.

vcc_err_unref
	- Units: bool
	- Default: on

	Unreferenced VCL objects result in error.

vcl_dir
	- Default: /usr/local/etc/varnish

	Directory from which relative VCL filenames (vcl.load and include) are opened.

vcl_trace
	- Units: bool
	- Default: off

	Trace VCL execution in the shmlog.
	Enabling this will allow you to see the path each request has taken through the VCL program.
	This generates a lot of logrecords so it is off by default.

vmod_dir
	- Default: /usr/local/lib/varnish/vmods

	Directory where VCL modules are to be found.

waiter
	- Default: default
	- Flags: must_restart, experimental

	Select the waiter kernel interface.


