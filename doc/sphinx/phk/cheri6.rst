.. _phk_cheri_6:

How Varnish met CHERI 6/N
=========================

Varnish Socket Addresses
------------------------

Socket addresses are a bit of a mess, in particular because nobody
dared shake up all the IPv4 legacy code when IPv6 was introduced.

In varnish we encapsulate all that ugliness in a ``struct suckaddr``,
so named because it sucks that we have to spend time and code on this.

In a case like this, it makes sense to make the internals strictly
read-only, to ensure nobody gets sneaky ideas:

.. code-block:: none

    struct suckaddr *
    VSA_Build(void *d, const void *s, unsigned sal)
    {
        struct suckaddr *sua;
     
        [… lots of ugly stuff …]
    
        return (RO(sua));
    }

It would then seem logical to use C's ``const`` to signal this fact,
but since the current VSA api is currently defined such that users
call ``free(3)`` on the suckaddrs when they are done with them, that does
not work, because the prototype for ``free(3)`` is:

.. code-block:: none

	void free(void *ptr);

So you cannot call it with a ``const`` pointer.

All hail the ISO-C standards committee!

This brings me to a soft point with CHERI: Allocators.

How to free things with CHERI
-----------------------------

A very common design-pattern in encapsulating classes look something
like this:

.. code-block:: none

    struct real_foo {
        struct foo foo;
        [some metadata about foo]
    };
    
    const struct foo *
    Make_Foo([arguments])
    {
        struct real_foo *rf;
    
        rf = calloc(1, sizeof *rf);
        if (rf == NULL)
            return (rf);
        [fill in rf]
        return (&rf->foo);
    }

    void
    Free_Foo(const struct foo **ptr)
    {
        const struct foo *fp;
        struct real_foo *rfp;

        assert(ptr != NULL);
        fp = *ptr;
        assert(fp != NULL);
        *ptr = NULL;

        rfp = (struct real_foo*)((uintptr_t)fp);
        [clean stuff up]
    }

We pass a ``**ptr`` to ``Free_Foo()``, another varnish style-pattern,
so we can NULL out the holding variable in the calling function,
to avoid a dangling pointer to the now destroyed object from
causing any kind of trouble later.

In the calling function this looks like:

.. code-block:: none

    const struct foo *foo_ptr;
    […]
    Free_Foo(&foo_ptr);

If we use CHERI to make the foo truly ``const`` for the users of
the API, we cannot, as above, wash the ``const`` away with a trip through
``uintptr_t`` and then write to the metadata.

The CHERI C/C++ manual, a terse academic tome, laconically mention that:

*»Therefore, some additional work may be required to derive
a pointer to the allocation’s metadata via another global capability,
rather than the one that has been passed to free().«*

Despite the understatement, I am very much in favour of this, because
this is *precisely* why my own
`phkmalloc <https://papers.freebsd.org/1998/phk-malloc/>`_
became a big hit twenty years ago:  By firmly separating the metadata
from the allocated space, several new classes of mistakes using the
``malloc(3)`` API could, and were, detected.

But this *is* going to be an embuggerance for CHERI users, because
with CHERI getting from one pointer to different one is actual work.

The only "proper" solution is to build some kind of datastructure:
List, tree, hash, DB2 database, pick any poison you prefer, and
search out the metadata pointer using the impotent pointer as key.
Given that CHERI pointers are huge, it may be a better idea to embed
a numeric index in the object and use that as the key.

An important benefit of this »additional work« is that if your
free-function gets passed a pointer to something else, you will
find out, because it is not in your data-structure.

It would be a good idea if CHERI came with a quality implementation
of "Find my other pointer", so that nobody is forced to crack The
Art of Computer Programming open for this.

When the API is "fire-and-forget" like VSA, in the sense that there
is no metadata to clean up, we can leave the hard work to the
``malloc(3)`` implementation.

Ever since ``phkmalloc`` no relevant implementation of ``malloc(3)``
has dereferenced the freed pointer, in order to find the metadata
for the allocation.  Despite its non-const C prototype ``free(3)``,
will therefore happily handle a ``const`` or even CHERIed read-only
pointer.

But you *will* have to scrub the ``const`` off with a ``uintptr_t``
to satisfy the C-compiler:

.. code-block:: none

    void
    VSA_free(const struct suckaddr **vsap)
    {       
        const struct suckaddr *vsa;
         
        AN(vsap);
        vsa = *vsap;
        *vsap = NULL;
        free((char *)(uintptr_t)vsa);
    }

Or in varnish style:

.. code-block:: none

    void
    VSA_free(const struct suckaddr **vsap)
    {
        const struct suckaddr *vsa;
    
        TAKE_OBJ_NOTNULL(vsa, vsap, SUCKADDR_MAGIC);
        free(TRUST_ME(vsa));
    }


Having been all over this code now, I have decided to constify ``struct
suckaddr`` in varnish, even without CHERI, it is sounder that way.

It is not bug, but CHERI made it a lot simpler and faster for me
to explore the consequences of this change, so I will forfeit
a score of "half a bug" to CHERI at this point.

*/phk*
