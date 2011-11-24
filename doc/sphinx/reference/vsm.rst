%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
Shared Memory Logging and Statistics
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

Varnish uses shared memory for logging and statistics, because it
is faster and much more efficient.  But it is also tricky in ways
a regular logfile is not.

When you open a file in "append" mode, the operating system guarantees
that whatever you write will not overwrite existing data in the file.
The neat result of this is that multiple procesess or threads writing
to the same file does not even need to know about each other, it all
works just as you would expect.

With a shared memory log, we get no help from the kernel, the writers
need to make sure they do not stomp on each other, and they need to
make it possible and safe for the readers to access the data.

The "CS101" way, is to introduce locks, and much time is spent examining
the relative merits of the many kinds of locks available.

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

When Varnishd starts, if it finds an existing shared memory file,
and it can safely read the master_pid field, it will check if that
process is running, and if so, fail with an error message, indicating
that -n arguments collide.

In all other cases, it will delete and create a new shmlog file,
in order to provide running readers a cooling off period, where
they can discover that there is a new shmlog file, by doing a
stat(2) call and checking the st_dev & st_inode fields.

Allocations
-----------

Sections inside the shared memory file are allocated dynamically,
for instance when a new backend is added.

While changes happen to the linked list of allocations, the "alloc_seq"
header field is zero, and after the change, it gets a value different
from what it had before.

Deallocations
-------------

When a section is freed, its class will change to "Cool" for at
least 10 seconds, giving programs using it time to detect the 
change in alloc_seq header field and/or the change of class.

