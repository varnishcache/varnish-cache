..
	Copyright (c) 2012-2020 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

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

default
~~~~~~~

syntax: default[,size]

The default storage backend is an alias to umem, where available, or
malloc otherwise.

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

.. _guide-storage_umem:

umem
~~~~

syntax: umem[,size]

Umem is a better alternative to the malloc backend where `libumem`_ is
available. All other configuration aspects are considered equal to
malloc.

`libumem`_ implements a slab allocator similar to the kernel memory
allocator used in virtually all modern operating systems and is
considered more efficient and scalable than classical
implementations. In particular, `libumem`_ is included in the family
of OpenSolaris descendent operating systems where jemalloc(3) is not
commonly available.

If `libumem`_ is not used otherwise, Varnish will only use it for
storage allocations and keep the default libc allocator for all other
Varnish memory allocation purposes.

If `libumem`_ is already loaded when Varnish initializes, this message
is output::

  notice: libumem was already found to be loaded
          and will likely be used for all allocations

to indicate that `libumem`_ will not only be used for storage. Likely
reasons for this to be the case are:

* some library ``varnishd`` is linked against was linked against
  `libumem`_ (most likely ``libpcre2-8``, check with ``ldd``)

* ``LD_PRELOAD_64=/usr/lib/amd64/libumem.so.1``,
  ``LD_PRELOAD_32=/usr/lib/libumem.so.1`` or
  ``LD_PRELOAD=/usr/lib/libumem.so.1`` is set

Varnish will also output this message to recommend settings for using
`libumem`_ for all allocations::

  it is recommended to set UMEM_OPTIONS=perthread_cache=0,backend=mmap
  before starting varnish

This recommendation should be followed to achieve an optimal
`libumem`_ configuration for Varnish. Setting this environment
variable before starting Varnish is required because `libumem`_ cannot
be reconfigured once loaded.

.. _libumem: http://dtrace.org/blogs/ahl/2004/07/13/number-11-of-20-libumem/

file
~~~~

syntax: file,path[,size[,granularity[,advice]]]

The file backend stores objects in virtual memory backed by an
unlinked file on disk with `mmap`, relying on the kernel to handle
paging as parts of the file are being accessed.

This implies that sufficient *virtual* memory needs to be available to
accomodate the file size in addition to any memory Varnish requires
anyway. Traditionally, the virtual memory limit is configured with
``ulimit -v``, but modern operating systems have other abstractions
for this limit like control groups (Linux) or resource controls
(Solaris).

.. XXX idk about the BSD and macOS abstractions -- slink

The 'path' parameter specifies either the path to the backing file or
the path to a directory in which `varnishd` will create the backing file.

The size parameter specifies the size of the backing file. The size
is assumed to be in bytes, unless followed by one of the following
suffixes:

      K, k    The size is expressed in kibibytes.

      M, m    The size is expressed in mebibytes.

      G, g    The size is expressed in gibibytes.

      T, t    The size is expressed in tebibytes.

If 'path' points to an existing file and no size is specified, the
size of the existing file will be used. If 'path' does not point to an
existing file it is an error to not specify the size.

If the backing file already exists, it will be truncated or expanded
to the specified size.

Note that if `varnishd` has to create or expand the file, it will not
pre-allocate the added space, leading to fragmentation, which may
adversely impact performance on rotating hard drives.  Pre-creating
the storage file using `dd(1)` will reduce fragmentation to a minimum.

.. XXX:1? benc

The 'granularity' parameter specifies the granularity of
allocation. All allocations are rounded up to this size. The granularity
is assumed to be expressed in bytes, unless followed by one of the
suffixes described for size.

The default granularity is the VM page size. The size should be reduced if you
have many small objects.

File performance is typically limited to the write speed of the
device, and depending on use, the seek time.

The 'advice' parameter tells the kernel how `varnishd` expects to
use this mapped region so that the kernel can choose the appropriate
read-ahead and caching techniques.  Possible values are ``normal``,
``random`` and ``sequential``, corresponding to MADV_NORMAL, MADV_RANDOM
and MADV_SEQUENTIAL madvise() advice argument, respectively.  Defaults to
``random``.

On Linux, large objects and rotational disk should benefit from
"sequential".

deprecated_persistent
~~~~~~~~~~~~~~~~~~~~~

syntax: deprecated_persistent,path,size {experimental}

*Before using, read* :ref:`phk_persistent`\ *!*

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
bans can cause problems. This is due to the fact that bans added while
the silo was offline will not be applied to the silo when it reenters
the cache. Consequently enabling previously banned objects to
reappear.

Transient Storage
-----------------

If you name any of your storage backend "Transient" it will be used
for transient (short lived) objects. This includes the temporary
objects created when returning a synthetic object. By default Varnish
would use an unlimited malloc backend for this.

.. XXX: Is this another parameter? In that case handled in the same manner as above? benc

Varnish will consider an object short lived if the TTL is below the
parameter 'shortlived'.


.. XXX: I am generally missing samples of setting all of these parameters, maybe one sample per section or a couple of examples here with a brief explanation to also work as a summary? benc
