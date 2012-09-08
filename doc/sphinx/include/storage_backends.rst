
malloc
~~~~~~

syntax: malloc[,size]

Malloc is a memory based backend. Each object will be allocated from
memory. If your system runs low on memory swap will be used. Be aware
that the size limitation only limits the actual storage and that
approximately 1k of memory per object will be used for various
internal structures.

The size parameter specifies the maximum amount of memory varnishd
will allocate.  The size is assumed to be in bytes, unless followed by
one of the following suffixes:

      K, k    The size is expressed in kibibytes.

      M, m    The size is expressed in mebibytes.

      G, g    The size is expressed in gibibytes.

      T, t    The size is expressed in tebibytes.

The default size is unlimited. 

Mallocs performance is bound by memory speed so it is very fast. 

file
~~~~

syntax: file[,path[,size[,granularity]]]

The file backend stores objects in memory backed by a file on disk
with mmap. This is the default storage backend and unless you specify
another storage this one will used along with Transient storage.

The path parameter specifies either the path to the backing file or
the path to a directory in which varnishd will create the backing
file.  The default is /tmp.

The size parameter specifies the size of the backing file.  The size
is assumed to be in bytes, unless fol‐ lowed by one of the following
suffixes:

      K, k    The size is expressed in kibibytes.

      M, m    The size is expressed in mebibytes.

      G, g    The size is expressed in gibibytes.

      T, t    The size is expressed in tebibytes.

      %       The size is expressed as a percentage of the free space on the
              file system where it resides.

The default size is 50%.

If the backing file already exists, it will be truncated or expanded
to the specified size.

Note that if varnishd has to create or expand the file, it will not
pre-allocate the added space, leading to fragmentation, which may
adversely impact performance.  Pre-creating the storage file using
dd(1) will reduce fragmentation to a minimum.

The granularity parameter specifies the granularity of
allocation.  All allocations are rounded up to this size.  The
is assumed to be in bytes, unless followed by one of the
suffixes described for size except for %.

The default size is the VM page size.  The size should be reduced if
you have many small objects.

File performance is typically limited by the write speed of the
device, and depending on use, the seek time.

persistent (experimental)
~~~~~~~~~~~~~~~~~~~~~~~~~

syntax: persistent,path,size {experimental}

Persistent storage. Varnish will store objects in a file in a manner
that will secure the survival of *most* of the objects in the event of
a planned or unplanned shutdown of Varnish.

The path parameter specifies the path to the backing file. If
the file doesn't exist Varnish will create it.

The size parameter specifies the size of the backing file.  The
size is assumed to be in bytes, unless followed by one of the
following suffixes:

      K, k    The size is expressed in kibibytes.

      M, m    The size is expressed in mebibytes.

      G, g    The size is expressed in gibibytes.

      T, t    The size is expressed in tebibytes.

Varnish will split the file into logical *silos* and write to the
silos in the manner of a circular buffer. Only one silo will be kept
open at any given point in time. Full silos are *sealed*. When Varnish
starts after a shutdown it will discard the content of any silo that
isn't sealed.

Transient Storage
-----------------
      
If you name any of your storage backend "Transient" it will be
used for transient (short lived) objects. By default Varnish
would use an unlimited malloc backend for this.
