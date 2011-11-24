%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
VSM: Shared Memory Logging and Statistics
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

Varnish uses shared memory to export parameters, logging and
statistics, because it is faster and much more efficient than
regular files.

"Varnish Shared Memory" or VSM, is the overall mechanism, which
manages a number of allocated "chunks" inside the same shared
memory file.

Each Chunk is just a slap of memory, which has
a three-part name (class, type, ident) and a length.

The Class indicates what type of data is stored in the chunk,
for instance "Arg" for command line arguments useful for
establishing an CLI connection to the varnishd, "Stat" for
statistics counters (VSC) and "Log" for log records (VSL).

The type and ident name parts are mostly used with stats
counters, where they identify dynamic counters, such as:

	SMA.Transient.c_bytes

The size of the VSM is a parameter, but changes only take
effect when the child process is restarted.

Shared memory trickery
----------------------

Shared memory is faster than regular files, but it is also slightly
tricky in ways a regular logfile is not.

When you open a file in "append" mode, the operating system guarantees
that whatever you write will not overwrite existing data in the file.
The neat result of this is that multiple procesess or threads writing
to the same file does not even need to know about each other, it all
works just as you would expect.

With a shared memory log, we get no such help from the kernel, the
writers need to make sure they do not stomp on each other, and they
need to make it possible and safe for the readers to access the
data.

The "CS101" way to deal with that, is to introduce locks, and much
time is spent examining the relative merits of the many kinds of
locks available.

Inside the varnishd (worker) process, we use mutexes to guarantee
consistency, both with respect to allocations, log entries and stats
counters.

We do not want a varnishncsa trying to push data through a stalled
ssh connection to stall the delivery of content, so readers like
that are purely read-only, they do not get to affect the varnishd
process and that means no locks for them.

Instead we use "stable storage" concepts, to make sure the view
seen by the readers is consistent at all times.

As long as you only add stuff, that is trivial, but taking away
stuff, such as when a backend is taken out of the configuration,
we need to give the readers a chance to discover this, a "cooling
off" period.

The Varnish way:
----------------

If Varnishd starts, and finds a locked shared memory file, it will
exit with a message about using different -n arguments if you want
multiple instances of varnishd.

Otherwise, it will create a new shared memory file each time it
starts a child process, since that marks a clean break in operation
anyway.

To the extent possible, old shared memory files are marked as
abandoned by setting the alloc_seq field to zero, which should be
monitored by all readers of the VSM.

Processes subscribing to VSM files for a long time, should notice
if the VSM file goes "silent" and check that the file has not been
renamed due to a child restart.

Chunks inside the shared memory file form a linked list, and whenever
that list changes, the alloc_seq field changes.

The linked list and other metadata in the VSM file, works with
offsets relative to the start address of where the VSM file is
memory mapped, so it need not be mapped at any particular address.

When new chunks are allocated, for instance when a new backend is
added, they are appended to the list, no matter where they are
located in the VSM file.

When a chunk is freed, it will be taken out of the linked list of
allocations, its length will be set to zero and alloc_seq will be
changed to indicate a change of layout.  For the next 60 seconds
the chunk will not be touched or reused, giving other subscribers
a chance to discover the deallocation.

The include file <vapi/vsm.h> provides the supported API for accessing
VSM files.
