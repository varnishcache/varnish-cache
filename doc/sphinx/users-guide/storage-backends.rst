.. _guide-storage:

Storage backends
----------------


Intro
~~~~~

Varnish has pluggable storage backends. It can store data in various
backends which can have different performance characteristics. The default
configuration is to use the malloc backend with a limited size. For a
serious Varnish deployment you probably would want to adjust the storage
settings.

malloc
~~~~~~

syntax: malloc[,size]

Malloc is a memory based backend. Each object will be allocated from
memory. If your system runs low on memory swap will be used.

Be aware that the size limitation only limits the actual storage and that the
approximately 1k of memory per object, used for various internal
structures, is included in the actual storage as well.

.. XXX:This seems to contradict the last paragraph in "sizing-your-cache". benc

The size parameter specifies the maximum amount of memory `varnishd`
will allocate.  The size is assumed to be in bytes, unless followed by
one of the following suffixes:

      K, k    The size is expressed in kibibytes.

      M, m    The size is expressed in mebibytes.

      G, g    The size is expressed in gibibytes.

      T, t    The size is expressed in tebibytes.

The default size is unlimited.

malloc's performance is bound to memory speed so it is very fast. If
the dataset is bigger than available memory performance will
depend on the operating systems ability to page effectively.

file
~~~~

syntax: file[,path[,size[,granularity]]]

The file backend stores objects in memory backed by an unlinked file on disk
with `mmap`.

The 'path' parameter specifies either the path to the backing file or
the path to a directory in which `varnishd` will create the backing
file. The default is `/tmp`.

The size parameter specifies the size of the backing file. The size
is assumed to be in bytes, unless followed by one of the following
suffixes:

      K, k    The size is expressed in kibibytes.

      M, m    The size is expressed in mebibytes.

      G, g    The size is expressed in gibibytes.

      T, t    The size is expressed in tebibytes.

      %       The size is expressed as a percentage of the free space on the
              file system where it resides.

The default size is to use 50% of the space available on the device.

If the backing file already exists, it will be truncated or expanded
to the specified size.

Note that if `varnishd` has to create or expand the file, it will not
pre-allocate the added space, leading to fragmentation, which may
adversely impact performance on rotating hard drives.  Pre-creating
the storage file using `dd(1)` will reduce fragmentation to a minimum.

.. XXX:1? benc

The 'granularity' parameter specifies the granularity of
allocation. All allocations are rounded up to this size. The granularity is
is assumed to be expressed in bytes, unless followed by one of the
suffixes described for size except for %.

The default granularity is the VM page size. The size should be reduced if you
have many small objects.

File performance is typically limited to the write speed of the
device, and depending on use, the seek time.

persistent (experimental)
~~~~~~~~~~~~~~~~~~~~~~~~~

syntax: persistent,path,size {experimental}

Persistent storage. Varnish will store objects in a file in a manner
that will secure the survival of *most* of the objects in the event of
a planned or unplanned shutdown of Varnish.

The 'path' parameter specifies the path to the backing file. If
the file doesn't exist Varnish will create it.

The 'size' parameter specifies the size of the backing file. The
size is expressed in bytes, unless followed by one of the
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

Note that taking persistent silos offline and at the same time using
bans can cause problems. This is due to the fact that bans added while the silo was
offline will not be applied to the silo when it reenters the cache. Consequently enabling
previously banned objects to reappear.

Transient Storage
-----------------

If you name any of your storage backend "Transient" it will be
used for transient (short lived) objects. By default Varnish
would use an unlimited malloc backend for this.

.. XXX: Is this another paramater? In that case handled in the same manner as above? benc

Varnish will consider an object short lived if the TTL is below the
parameter 'shortlived'.


.. XXX: I am generally missing samples of setting all of these parameters, maybe one sample per section or a couple of examples here with a brief explanation to also work as a summary? benc
