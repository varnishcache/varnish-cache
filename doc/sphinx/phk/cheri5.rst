.. _phk_cheri_5:

How Varnish met CHERI 5/N
=========================

Varnish Workspaces
------------------

To process a HTTP request or response, varnish must allocate bits
of memory which will only be used for the duration of that processing,
and all of it can be released back at the same time.

To avoid calling ``malloc(3)`` a lot, which comes with a locking
overhead in a heavily multithreaded process, but even more to
avoid having to keep track of all these allocations in order to be able
to ``free(3)`` them all, varnish has "workspaces":

.. code-block:: none

    struct ws {
        […]
        char    *s;     /* (S)tart of buffer */
        char    *f;     /* (F)ree/front pointer */
        char    *r;     /* (R)eserved length */
        char    *e;     /* (E)nd of buffer */
    };

The ``s`` pointer points at the start of a slab of memory, owned
exclusively by the current thread and ``e`` points to the end.

Initially ``f`` is the same as ``s``, but as allocations are made
from the workspace, it moves towards ``e``.  The ``r`` pointer is
used to make "reservations", we will ignore that for now.

Workspaces look easy to create:

.. code-block:: none

    ws->s = space;
    ws->e = ws->s + len;
    ws->f = ws->s;
    ws->r = NULL;

… only, given the foot-shooting-abetting nature of the C language,
we have bolted on a lot of seat-belts:

.. code-block:: none

    #define WS_ID_SIZE 4
    
    struct ws {
        unsigned        magic;
    #define WS_MAGIC    0x35fac554
        char            id[WS_ID_SIZE]; /* identity */
        char            *s;             /* (S)tart of buffer */
        char            *f;             /* (F)ree/front pointer */
        char            *r;             /* (R)eserved length */
        char            *e;             /* (E)nd of buffer */
    };
    
    void
    WS_Init(struct ws *ws, const char *id, void *space, unsigned len)
    {
        unsigned l;
    
        DSLb(DBG_WORKSPACE,
            "WS_Init(%s, %p, %p, %u)", id, ws, space, len);
        assert(space != NULL);
        assert(PAOK(space));
        INIT_OBJ(ws, WS_MAGIC);
        ws->s = space;
        l = PRNDDN(len - 1);
        ws->e = ws->s + l;
        memset(ws->e, WS_REDZONE_END, len - l);
        ws->f = ws->s;
        assert(id[0] & 0x20);           // cheesy islower()
        bstrcpy(ws->id, id);
        WS_Assert(ws);
    }

Let me walk you through that:

The ``DSLb()`` call can be used to trace all operations on the
workspace, so we can see what actually goes on.

(Hint: Your ``malloc(3)`` may have something similar,
look for ``utrace`` in the manual page.)

Next we check the provided space pointer is not NULL, and
that it is properly aligned, these are both following
a varnish style-pattern, to sprinkle asserts liberally,
both as code documentation, but also because it allows
the compiler to optimize things better.

The ``INIT_OBJ() and ``magic`` field is a style-pattern
we use throughout varnish:  Each structure is tagged with
a unique magic, which can be used to ensure that pointers
are what we are told, when they get passed through a ``void*``.

We set the ``s`` pointer.

We calculate a length at least one byte shorter than what
we were provided, align it, and point ``e`` at that.

We fill that extraspace at and past ``e``, with a "canary" to
stochastically detect overruns.  It catches most but not
all overruns.

We set the name of the workspace, ensuring it is not already
marked as overflowed.

And finally check that the resulting workspace complies with
the defined invariants, as captured in the ``WS_Assert()``
function.

With CHERI, it looks like this:

.. code-block:: none

    void
    WS_Init(struct ws *ws, const char *id, void *space, unsigned len)
    {
        unsigned l;
 
        DSLb(DBG_WORKSPACE,
            "WS_Init(%s, %p, %p, %u)", id, ws, space, len);
        assert(space != NULL);
        INIT_OBJ(ws, WS_MAGIC);
        assert(PAOK(space));
        ws->s = cheri_bounds_set(space, len);
        ws->e = ws->s + len
        ws->f = ws->s;
        assert(id[0] & 0x20);           // cheesy islower()
        bstrcpy(ws->id, id);
        WS_Assert(ws);
    }

All the gunk to implement a canary to detect overruns went
away, because with CHERI we can restrict the ``s`` pointer so writing
outside the workspace is *by definition* impossible, as long as your
pointer is derived from ``s``.

Less memory wasted, much stronger check and more readable source-code,
what's not to like ?

When an allocation is made from the workspace, CHERI makes it possible
to restrict the returned pointer to just the allocated space:

.. code-block:: none

    void *
    WS_Alloc(struct ws *ws, unsigned bytes)
    {
        char *r;
     
        […]
        r = ws->f;
        ws->f += bytes;
        return(cheri_bounds_set(r, bytes));
    }

Varnish String Buffers
----------------------

Back in the mists of time, Dag-Erling Smørgrav and I designed a
safe string API called ``sbuf`` for the FreeBSD kernel.

The basic idea is you set up your buffer, you call functions to stuff
text into it, and those functions do all the hard work to ensure
you do not overrun the buffer.  When the string is complete, you
call a function to "finish" the buffer, and if returns a flag which
tells you if overrun (or other problems) happened, and then you can
get a pointer to the resulting string from another function.

Varnish has adopted sbuf's under the name ``vsb``.  This should
really not surprise anybody:  Dag-Erling was also involved
in the birth of varnish.

It should be obvious that internally ``vsb`` almost always operate
on a bigger buffer than the result, so this is another obvious
place to have CHERI cut a pointer down to size:

.. code-block:: none

    char *
    VSB_data(const struct vsb *s)
    {

        assert_VSB_integrity(s);
        assert_VSB_state(s, VSB_FINISHED);

        return (cheri_bounds_set(s->s_buf, s->s_len + 1));
    }

Still no bugs though.

*/phk*
