.. _ref-vmod:

%%%%%%%%%%%%%%%%%%%%%%
VMOD - Varnish Modules
%%%%%%%%%%%%%%%%%%%%%%

For all you can do in VCL, there are things you can not do.
Look an IP number up in a database file for instance.
VCL provides for inline C code, and there you can do everything,
but it is not a convenient or even readable way to solve such
problems.

This is where VMODs come into the picture:   A VMOD is a shared
library with some C functions which can be called from VCL code.

For instance::

	import std;

	sub vcl_deliver {
		set resp.http.foo = std.toupper(req.url);
	}

The "std" vmod is one you get with Varnish, it will always be there
and we will put "boutique" functions in it, such as the "toupper"
function shown above.  The full contents of the "std" module is
documented in vmod_std(3).

This part of the manual is about how you go about writing your own
VMOD, how the language interface between C and VCC works, where you
can find contributed VMODs etc. This explanation will use the "std"
VMOD as example, having a Varnish source tree handy may be a good
idea.

VMOD Directory
==============

The VMOD directory is an up-to-date compilation of maintained
extensions written for Varnish Cache:

    https://www.varnish-cache.org/vmods

The vmod.vcc file
=================

The interface between your VMOD and the VCL compiler ("VCC") and the
VCL runtime ("VRT") is defined in the vmod.vcc file which a python
script called "vmodtool.py" turns into thaumaturgically challenged C
data structures that does all the hard work.

The std VMODs vmod.vcc file looks somewhat like this::

	$Module std 3
	$Event event_function
	$Function STRING toupper(STRING_LIST)
	$Function STRING tolower(STRING_LIST)
	$Function VOID set_ip_tos(INT)

The first line gives the name of the module and the manual section where
the documentation will reside.

The second line specifies an optional "Event" function, which will be
called whenever a VCL program which imports this VMOD is loaded or
transitions to any of the warm, active, cold or discarded states.
More on this below.

The next three lines define three functions in the VMOD, along with the
types of the arguments, and that is probably where the hardest bit of
writing a VMOD is to be found, so we will talk about that at length in
a moment.

Notice that the third function returns VOID, that makes it a "procedure"
in VCL lingo, meaning that it cannot be used in expressions, right side
of assignments and such.  Instead it can be used as a primary action,
something functions which return a value can not::

	sub vcl_recv {
		std.set_ip_tos(32);
	}

Running vmodtool.py on the vmod.vcc file, produces a "vcc_if.c" and
"vcc_if.h" files, which you must use to build your shared library
file.

Forget about vcc_if.c everywhere but your Makefile, you will never
need to care about its contents, and you should certainly never
modify it, that voids your warranty instantly.

But vcc_if.h is important for you, it contains the prototypes for
the functions you want to export to VCL.

For the std VMOD, the compiled vcc_if.h file looks like this::

	struct vmod_priv;

	VCL_STRING vmod_toupper(VRT_CTX, const char *, ...);
	VCL_STRING vmod_tolower(VRT_CTX, const char *, ...);
	VCL_VOID vmod_set_ip_tos(VRT_CTX, VCL_INT);

	vmod_event_f event_function;

Those are your C prototypes.  Notice the ``vmod_`` prefix on the
function names.

Named arguments and default values
----------------------------------

The basic vmod.vcc function declaration syntax introduced above makes all
arguments mandatory for calls from vcl - which implies that they need
to be given in order.

Naming the arguments as in::

	$Function BOOL match_acl(ACL acl, IP ip)

allows calls from VCL with named arguments in any order, for example::

	if (debug.match_acl(ip=client.ip, acl=local)) { # ...

Named arguments also take default values, so for this example from
the debug vmod::

	$Function STRING argtest(STRING one, REAL two=2, STRING three="3",
				 STRING comma=",", INT four=4)

only argument `one` is required, so that all of the following are
valid invocations from vcl::

	debug.argtest("1", 2.1, "3a")
	debug.argtest("1", two=2.2, three="3b")
	debug.argtest("1", three="3c", two=2.3)
	debug.argtest("1", 2.4, three="3d")
	debug.argtest("1", 2.5)
	debug.argtest("1", four=6);

The C interface does not change with named arguments and default
values, arguments remain positional and defaul values appear no
different to user specified values.

`Note` that default values have to be given in the native C-type
syntax, see below. As a special case, ``NULL`` has to be given as ``0``.

.. _ref-vmod-vcl-c-types:

VCL and C data types
====================

VCL data types are targeted at the job, so for instance, we have data
types like "DURATION" and "HEADER", but they all have some kind of C
language representation.  Here is a description of them.

All but the PRIV and STRING_LIST types have typedefs: VCL_INT, VCL_REAL,
etc.

ACL
	C-type: ``const struct vrt_acl *``

	A type for named ACLs declared in VCL.

BACKEND
	C-type: ``const struct director *``

	A type for backend and director implementations. See
	:ref:`ref-writing-a-director`.

BLOB
	C-type: ``const struct vmod_priv *``

	An opaque type to pass random bits of memory between VMOD
	functions.

BOOL
	C-type: ``unsigned``

	Zero means false, anything else means true.

BYTES
	C-type: ``double``

	Unit: bytes.

	A storage space, as in 1024 bytes.

DURATION
	C-type: ``double``

	Unit: seconds.

	A time interval, as in 25 seconds.

ENUM
	vcc syntax: ENUM { val1, val2, ... }

	vcc example: ``ENUM { one, two, three } number="one"``

	C-type: ``const char *``

	Allows values from a set of constant strings. `Note` that the
	C-type is a string, not a C enum.

HEADER
	C-type: ``const struct gethdr_s *``

	These are VCL compiler generated constants referencing a
	particular header in a particular HTTP entity, for instance
	``req.http.cookie`` or ``beresp.http.last-modified``.  By passing
	a reference to the header, the VMOD code can both read and write
	the header in question.

	If the header was passed as STRING, the VMOD code only sees
	the value, but not where it came from.

HTTP
	C-type: ``struct http *``

	TODO

INT
	C-type: ``long``

	A (long) integer as we know and love them.

IP
	C-type: ``const struct suckaddr *``

	This is an opaque type, see the ``include/vsa.h`` file for
	which primitives we support on this type.

PRIV_CALL
	See :ref:`ref-vmod-private-pointers` below.

PRIV_TASK
	See :ref:`ref-vmod-private-pointers` below.

PRIV_TOP
	See :ref:`ref-vmod-private-pointers` below.

PRIV_VCL
	See :ref:`ref-vmod-private-pointers` below.

PROBE
	C-type: ``const struct vrt_backend_probe *``

	A named standalone backend probe definition.

REAL
	C-type: ``double``

	A floating point value.

STRING
	C-type: ``const char *``

	A NUL-terminated text-string.

	Can be NULL to indicate a nonexistent string, for instance in::

		mymod.foo(req.http.foobar);

	If there were no "foobar" HTTP header, the vmod_foo()
	function would be passed a NULL pointer as argument.

	When used as a return value, the producing function is
	responsible for arranging memory management.  Either by
	freeing the string later by whatever means available or
	by using storage allocated from the client or backend
	workspaces.

STEVEDORE
	C-type: ``const struct stevedore *``

	A storage backend.

STRING_LIST
	C-type: ``const char *, ...``

	A multi-component text-string.  We try very hard to avoid
	doing text-processing in Varnish, and this is one way we
	to avoid that, by not editing separate pieces of a string
	together to one string, unless we have to.

	Consider this contrived example::

		set req.http.foo = std.toupper(req.http.foo + req.http.bar);

	The usual way to do this, would be be to allocate memory for
	the concatenated string, then pass that to ``toupper()`` which in
	turn would return another freshly allocated string with the
	modified result.  Remember: strings in VCL are ``const``, we
	cannot just modify the string in place.

	What we do instead, is declare that ``toupper()`` takes a "STRING_LIST"
	as argument.  This makes the C function implementing ``toupper()``
	a vararg function (see the prototype above) and responsible for
	considering all the ``const char *`` arguments it finds, until the
	magic marker "vrt_magic_string_end" is encountered.

	Bear in mind that the individual strings in a STRING_LIST can be
	NULL, as described under STRING, that is why we do not use NULL
	as the terminator.

	Right now we only support STRING_LIST being the last argument to
	a function, we may relax that at a latter time.

	If you don't want to bother with STRING_LIST, just use STRING
	and make sure your workspace_client and workspace_backend params
	are big enough.

TIME
	C-type: ``double``

	Unit: seconds since UNIX epoch.

	An absolute time, as in 1284401161.

VOID
	C-type: ``void``

	Can only be used for return-value, which makes the function a VCL
	procedure.


.. _ref-vmod-private-pointers:

Private Pointers
================

It is often useful for library functions to maintain local state,
this can be anything from a precompiled regexp to open file descriptors
and vast data structures.

The VCL compiler supports the following private pointers:

* ``PRIV_CALL`` "per call" private pointers are useful to cache/store
  state relative to the specific call or its arguments, for instance a
  compiled regular expression specific to a regsub() statement or a
  simply caching the last output of some expensive lookup.

* ``PRIV_TASK`` "per task" private pointers are useful for state that
  applies to calls for either a specific request or a backend
  request. For instance this can be the result of a parsed cookie
  specific to a client. Note that ``PRIV_TASK`` contexts are separate
  for the client side and the backend side, so use in
  ``vcl_backend_*`` will yield a different private pointer from the
  one used on the client side.

* ``PRIV_TOP`` "per top-request" private pointers live for the
  duration of one request and all its ESI-includes. They are only
  defined for the client side. When used from backend VCL subs, a NULL
  pointer will be passed.

* ``PRIV_VCL`` "per vcl" private pointers are useful for such global
  state that applies to all calls in this VCL, for instance flags that
  determine if regular expressions are case-sensitive in this vmod or
  similar. The ``PRIV_VCL`` object is the same object that is passed
  to the VMOD's event function.

The way it works in the vmod code, is that a ``struct vmod_priv *`` is
passed to the functions where one of the ``PRIV_*`` argument types is
specified.

This structure contains three members::

	typedef void vmod_priv_free_f(void *);
	struct vmod_priv {
		void                    *priv;
		int			len;
		vmod_priv_free_f        *free;
	};

The "priv" element can be used for whatever the vmod code wants to
use it for, it defaults to a NULL pointer.

The "len" element is used primarily for BLOBs to indicate its size.

The "free" element defaults to NULL, and it is the modules responsibility
to set it to a suitable function, which can clean up whatever the "priv"
pointer points to.

When a VCL program is discarded, all private pointers are checked
to see if both the "priv" and "free" elements are non-NULL, and if
they are, the "free" function will be called with the "priv" pointer
as the only argument.

In the common case where a private data structure is allocated with
malloc would look like this::

	if (priv->priv == NULL) {
		priv->priv = calloc(1, sizeof(struct myfoo));
		AN(priv->priv);
		priv->free = free;	/* free(3) */
		mystate = priv->priv;
		mystate->foo = 21;
		...
	} else {
		mystate = priv->priv;
	}
	if (foo > 25) {
		...
	}

The per-call vmod_privs are freed before the per-vcl vmod_priv.

.. _ref-vmod-event-functions:

Event functions
===============

VMODs can have an "event" function which is called when a VCL which
imports the VMOD is loaded or discarded.  This corresponds to the
``VCL_EVENT_LOAD`` and ``VCL_EVENT_DISCARD`` events, respectively.
In addition, this function will be called when the VCL temperature is
changed to cold or warm, corresponding to the ``VCL_EVENT_COLD`` and
``VCL_EVENT_WARM`` events.

The first argument to the event function is a VRT context.

The second argument is the vmod_priv specific to this particular VCL,
and if necessary, a VCL specific VMOD "fini" function can be attached
to its "free" hook.

The third argument is the event.

If the VMOD has private global state, which includes any sockets or files
opened, any memory allocated to global or private variables in the C-code etc,
it is the VMODs own responsibility to track how many VCLs were loaded or
discarded and free this global state when the count reaches zero.

VMOD writers are *strongly* encouraged to release all per-VCL resources for a
given VCL when it emits a ``VCL_EVENT_COLD`` event. You will get a chance to
reacquire the resources before the VCL becomes active again and be notified
first with a ``VCL_EVENT_WARM`` event. Unless a user decides that a given VCL
should always be warm, an inactive VMOD will eventually become cold and should
manage resources accordingly.

An event function must return zero upon success. It is only possible to fail
an initialization with the ``VCL_EVENT_LOAD`` or ``VCL_EVENT_WARM`` events.
Should such a failure happen, a ``VCL_EVENT_DISCARD`` or ``VCL_EVENT_COLD``
event will be sent to the VMODs that succeeded to put them back in a cold
state. The VMOD that failed will not receive this event, and therefore must
not be left half-initialized should a failure occur.

If your VMOD is running an asynchronous background job you can hold a reference
to the VCL to prevent it from going cold too soon and get the same guarantees
as backends with ongoing requests for instance. For that, you must acquire the
reference by calling ``VRT_ref_vcl`` when you receive a ``VCL_EVENT_WARM`` and
later calling ``VRT_rel_vcl`` once the background job is over. Receiving a
``VCL_EVENT_COLD`` is your cue to terminate any background job bound to a VCL.

You can find an example of VCL references in vmod-debug::

	priv_vcl->vclref = VRT_ref_vcl(ctx, "vmod-debug");
	...
	VRT_rel_vcl(&ctx, &priv_vcl->vclref);

In this simplified version, you can see that you need at least a VCL-bound data
structure like a ``PRIV_VCL`` or a VMOD object to keep track of the reference
and later release it. You also have to provide a description, it will be printed
to the user if they try to warm up a cooling VCL::

	$ varnishadm vcl.list
	available  auto/cooling       0 vcl1
	active     auto/warm          0 vcl2

	$ varnishadm vcl.state vcl1 warm
	Command failed with error code 300
	Failed <vcl.state vcl1 auto>
	Message:
		VCL vcl1 is waiting for:
		- vmod-debug

In the case where properly releasing resources may take some time, you can
opt for an asynchronous worker, either by spawning a thread and tracking it, or
by using Varnish's worker pools.

When to lock, and when not to lock
==================================

Varnish is heavily multithreaded, so by default VMODs must implement
their own locking to protect shared resources.

When a VCL is loaded or unloaded, the event and priv->free are
run sequentially all in a single thread, and there is guaranteed
to be no other activity related to this particular VCL, nor are
there init/fini activity in any other VCL or VMOD at this time.

That means that the VMOD init, and any object init/fini functions
are already serialized in sensible order, and won't need any locking,
unless they access VMOD specific global state, shared with other VCLs.

Traffic in other VCLs which also import this VMOD, will be happening
while housekeeping is going on.

Updating VMODs
==============

A compiled VMOD is a shared library file which Varnish dlopen(3)'s
using flags RTLD_NOW | RTLD_LOCAL.

As a general rule, once a file is opened with dlopen(3) you should
never modify it, but it is safe to rename it and put a new file
under the name it had, which is how most tools installs and updates
shared libraries.

However, when you call dlopen(3) with the same filename multiple
times it will give you the same single copy of the shared library
file, without checking if it was updated in the meantime.

This is obviously an oversight in the design of the dlopen(3) library
function, but back in the late 1980s nobody could imagine why a
program would ever want to have multiple different versions of the
same shared library mapped at the same time.

Varnish does that, and therefore you must restart the worker process
before Varnish will discover an updated VMOD.

If you want to test a new version of a VMOD, while being able to
instantly switch back to the old version, you will have to install
each version with a distinct filename or in a distinct subdirectory
and use ``import foo from "...";`` to reference it in your VCL.

We're not happy about this, but have found no sensible workarounds.
