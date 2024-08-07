.. _phk_cheri_4:

How Varnish met CHERI 4/N
=========================

So what can CHERI do ?
----------------------

CHERI can restrict pointers, but it does not restrict the memory
they point to::

    #include <cheriintrin.h>
    #include <stdio.h>
    #include <string.h>

    int
    main()
    {
        char buf[20];
        char *ptr1 = cheri_perms_and(buf, CHERI_PERM_LOAD);
        char *ptr2 = buf;

        strcpy(buf, "Hello World\n");
        ptr1[5] = '_';    // Will core dump
        ptr2[5] = '_';    // Works fine.
        puts(buf);
        return (0);
    }

I suspect most programmers will find this counter-intuitive, because
normally it is the memory itself which write-protected in which
case no pointers can ever write to it.

This is where the word "capability" comes from:  The pointer is what
gives you the "capability" to access the memory, and therefore they
can be restricted separately from the memory they provide access to.

If you could just create your own "capabilities" out of integers,
that would be no big improvement, but you cannot:  Under CHERI you
can only make a new capability from another capability, and the new
one can never be more potent than the one it is derived from.

In addition to 'read' and 'write' permissions, capabilities contain the
start and the length of the piece of memory they allow access to.

Under CHERI the printf(3) pattern "%#p" tells the full story::

    #include <cheriintrin.h>
    #include <stdio.h>
    #include <string.h>
    
    int
    main()
    {
        char buf[20];
        char *ptr1 = cheri_perms_and(buf, CHERI_PERM_LOAD);
        char *ptr2 = buf;
        char *ptr3;
        char *ptr4;
    
        strcpy(buf, "Hello World\n");
        //ptr1[5] = '_';    // Will core dump
        ptr2[5] = '_';    // Works fine.
        puts(buf);
        printf("buf:\t%#p\n", buf);
        printf("ptr1:\t%#p\n", ptr1);
        printf("ptr2:\t%#p\n", ptr2);
        ptr3 = ptr2 + 1;
        printf("ptr3:\t%#p\n", ptr3);
        ptr4 = cheri_bounds_set(ptr3, 4);
        printf("ptr4:\t%#p\n", ptr4);
        return (0);
    }

And we get::

    buf:    0xfffffff7ff68 [rwRW,0xfffffff7ff68-0xfffffff7ff7c]
    ptr1:   0xfffffff7ff68 [r,0xfffffff7ff68-0xfffffff7ff7c]
    ptr2:   0xfffffff7ff68 [rwRW,0xfffffff7ff68-0xfffffff7ff7c]
    ptr3:   0xfffffff7ff69 [rwRW,0xfffffff7ff68-0xfffffff7ff7c]
    ptr4:   0xfffffff7ff69 [rwRW,0xfffffff7ff69-0xfffffff7ff6d]

(Ignore the upper-case 'RW' for now, I'll get back to those later.)

Because of the odd things people do with pointers in C, ``ptr3``
keeps the same range as ``ptr2`` and that's a good excuse to
address the question I imagine my readers have at this point:

What about ``const`` ?
----------------------

The C language is a mess.

Bjarne Stroustrup introduced ``const`` in "C-with-classes",
which later became C++, and in C++ it does the right thing.

The morons on the ISO-C committee did a half-assed import
in C89, and therefore it does the wrong thing::

    char *strchr(const char *s, int c);

You pass a read-only pointer in, and you get a read-write
pointer into that string back ?!

There were at least three different ways they could have done right:

* Add a ``cstrchr`` function, having ``const`` on both sides.

* Enable prototypes to explain this, with some gruesome hack::

	(const) char *strchr((const) char *s, int c);

* Allow multiple prototypes for the same function, varying only in const-ness::

    char *strchr(char *s, int c);
    const char *strchr(const char *s, int c);

But instead they just ignored that issue, and several others like it.

The result is that we have developed the concept of "const-poisoning",
to describe the fact that if you use "const" to any extent in your
C sources, you almost always end up needing a macro like::

    #define TRUST_ME(ptr)  ((void*)(uintptr_t)(ptr))

To remove const-ness where it cannot go.

(If you think that is ISO-C's opus magnum, ask yourself why we still
cannot specify struct packing and endianess explicitly ?  It's hardly
like anybody ever have to apart data-structures explicitly specified in 
hardware or protocol documents, is it ?)

Read/Write markup with CHERI
----------------------------

Because ``const`` is such a mess in C, the CHERI compiler does not
automatically remove the write-capability from ``const`` arguments
to functions, something I suspect (but have not checked) that they
can do in C++.

Instead we will have to do it ourselves, so I have added two macros to
our ``<vdef.h>`` file::

    #define RO(x) cheri_perms_and((x), CHERI_PERM_LOAD)
    #define ROP(x) cheri_perms_and((x), CHERI_PERM_LOAD|CHERI_PERM_LOAD_CAP)

Passing a pointer through the ``RO()`` macro makes it read-only, so we
can do stuff like::

    @@ -285,7 +286,7 @@ VRT_GetHdr(VRT_CTX, VCL_HEADER hs)
        […]
    -   return (p);
    +   return (RO(p));
     }

To explicitly give ``const`` some bite.

The difference between ``RO`` and ``ROP`` is where the upper- and
lower-case "rw" comes into the picture:  Capabilities have two
levels of read/write protection:

* Can you read or write normal data with this pointer (``CHERI_PERM_LOAD``)

* Can you read or write pointers with this pointer (``CHERI_PERM_LOAD_CAP``)

Rule of thumb: Pure data: Use only the first, structs with pointers in them,
use both.

One can also make write-only pointers with CHERI, but there are
only few places where they can be gainfully employed, outside strict
security in handling of (cryptographic) secrets.

Right now I'm plunking ``RO()`` and ``ROP()`` into the varnish code,
and one by one relearning what atrocity the 37 uses of ``TRUST_ME()``
hide.

Still no bugs found.

*/phk*
