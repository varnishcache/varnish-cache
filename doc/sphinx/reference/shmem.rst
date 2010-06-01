%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
Shared Memory Logging and Statistics
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

Varnish uses shared memory for logging and statistics, because it
is faster and much more efficient.  But it is also tricky in ways
a regular logfile is not.

Collision Detection
-------------------

When you open a file in "append" mode, the operating system guarantees
that whatever you write will not overwrite existing data in the file.
The neat result of this is that multiple procesess or threads writing
to the same file does not even need to know about each other it all
works just as you would expect.

With shared memory you get no such seatbelts.

When Varnishd starts, it could find an existing shared memory file,
being used by another varnishd, either because somebody gave the wrong
(or no) -n argument, or because the old varnishd was not dead when
some kind of process-nanny restarted varnishd anew.

If the shared memory file has a different version or layout it will
be deleted and a new created.

If the process listed in the "master_pid" field is running,
varnishd will abort startup, assuming you got a wrong -n argument.

If the process listed in the "child_pid" field is (still?) running,
or if the file as a size different from that specified in the -l 
argument, it will be deleted and a new file created.

The upshot of this, is that programs subscribing to the shared memory
file should periodically do a stat(2) on the name, and if the
st_dev or st_inode fields changed, switch to the new shared memory file.

Also, the master_pid field should be monitored, if it changes, the
shared memory file should be "reopened" with respect to the linked
list of allocations.

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

