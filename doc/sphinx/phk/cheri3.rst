.. _phk_cheri_3:

How Varnish met CHERI 3/N
=========================

Of the four tests which still fail, three use the "file stevedore".

Things you cannot do under CHERI: Pointers in shared files
----------------------------------------------------------

In varnish a "stevedore" is responsible for orchestrating storage
for the cached objects, and it is an API extension-point.

The "malloc stevedore" is the default, it does precisely what you
think it does.

The "file" stevedore, ``mmap(2)``'s a file in the filesystem
and partitions that out as needed, pretty much like an implementation
of ``malloc(3)`` would do.

The "file" stevedore exists mainly because back in 2006 mapping a
file with ``MAP_NOCORE``, was the only way to avoid the entire
object cache being included in core-dumps.

But CHERI will not allow you to put a pointer into a regular file
``mmap(2)``'ed ``MAP_SHARED``, because that would allow another
process, maybe even on a different computer, to ``mmap(2)`` the
file ``MAP_SHARED`` later and, by implication, resurrect the pointers.

The "persistent" stevedore mentioned in part 1, does the same thing,
and does not work under CHERI for the same reason.

If instead we map the file ``MAP_PRIVATE``, nobody else will
ever be able to see the pointers, and those three test cases pass::

    MAP_SHARED pointers
    =====================
    TEST tests/b00005.vtc
    TEST tests/r02429.vtc
    TEST tests/s00003.vtc

(We cannot do the same for the "persistent" stevedore, because the
only reason it exists is precisely to to resurrect those pointers later.)

The final and really nasty test-case
------------------------------------

As previously mentioned, when you have 100K threads, you have to be
stingy with memory, in particular with the thread stacks.

But if you tune things too hard, the threads will step out of their
stack and coredumps result.  We try to be a little bit helpful
about the diagnostics in such cases, and we have a test-case
which tries to exercise that.

That test-case is a pretty nasty piece of work, and for all
intents and purposes, it is just a cheap erzats of what CHERI is
supposed to do for us, so I am going to punt on it, and get
to the more interesting parts of this project::

    SigSegv handler test
    =====================
    TEST tests/c00057.vtc


*/phk*
