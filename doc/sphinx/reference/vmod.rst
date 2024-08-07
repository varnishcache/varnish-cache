..
	Copyright (c) 2010-2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _ref-vmod:

%%%%%%%%%%%%%%%%%%%%%%
VMOD - Varnish Modules
%%%%%%%%%%%%%%%%%%%%%%

For all you can do in VCL, there are things you cannot do.
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
data structures that do all the hard work.

The std VMODs vmod.vcc file looks somewhat like this::

	$ABI strict
	$Module std 3 "Varnish Standard Module"
	$Event event_function
	$Function STRING toupper(STRANDS s)
	$Function STRING tolower(STRANDS s)
	$Function VOID set_ip_tos(INT)

The ``$ABI`` line is optional.  Possible values are ``strict``
(default) and ``vrt``.  It allows to specify that a vmod is integrating
with the blessed ``vrt`` interface provided by ``varnishd`` or go
deeper in the stack.

As a rule of thumb you, if the VMOD uses more than the VRT (Varnish
RunTime), in which case it needs to be built for the exact Varnish
version, use ``strict``.  If it complies to the VRT and only needs
to be rebuilt when breaking changes are introduced to the VRT API,
use ``vrt``.

The ``$Module`` line gives the name of the module, the manual section
where the documentation will reside, and the description.

The ``$Event`` line specifies an optional "Event" function, which
will be called whenever a VCL program which imports this VMOD is
loaded or transitions to any of the warm, active, cold or discarded
states.  More on this below.

The ``$Function`` lines define three functions in the VMOD, along
with the types of the arguments, and that is probably where the
hardest bit of writing a VMOD is to be found, so we will talk about
that at length in a moment.

Notice that the third function returns VOID, that makes it a "procedure"
in VCL lingo, meaning that it cannot be used in expressions, right side
of assignments and such.  Instead it can be used as a primary action,
something functions which return a value cannot::

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

	VCL_STRING vmod_toupper(VRT_CTX, VCL_STRANDS);
	VCL_STRING vmod_tolower(VRT_CTX, VCL_STRANDS);
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
values, arguments remain positional and default values appear no
different to user specified values.

`Note` that default values have to be given in the native C-type
syntax, see below. As a special case, ``NULL`` has to be given as ``0``.

Optional arguments
------------------

The vmod.vcc declaration also allows for optional arguments in square
brackets like so::

	$Function VOID opt(PRIV_TASK priv, INT four = 4, [STRING opt])

With any optional argument present, the C function prototype looks
completely different:

	* Only the ``VRT_CTX`` and object pointer arguments (only for
	  methods) remain positional

	* All other arguments get passed in a struct as the last
	  argument of the C function.

The argument struct is simple, vmod authors should check the
`vmodtool`-generated ``vcc_if.c`` file for the function and struct
declarations:

	* for each optional argument, a ``valid_``\ `argument` member
	  is used to signal the presence of the respective optional
	  argument.

	  ``valid_`` argstruct members should only be used as truth
	  values, irrespective of their actual data type.

	* named arguments are passed in argument struct members by the
	  same name and with the same data type.

	* unnamed (positional) arguments are passed as ``arg``\ `n`
	  with `n` starting at 1 and incrementing with the argument's
	  position.

.. _ref-vmod-vcl-c-objects:

Objects and methods
-------------------

Varnish also supports a simple object model for vmods. Objects and
methods are declared in the vcc file as::

	$Object class(...)
	$Method .method(...)


For declared object classes of a vmod, object instances can then be
created in ``vcl_init { }`` using the ``new`` statement::

	sub vcl_init {
		new foo = vmod.class(...);
	}

and have their methods called anywhere (including in ``vcl_init {}``
after the instantiation)::

	sub somewhere {
		foo.method(...);
	}

Nothing prevents a method to be named like the constructor and the
meaning of such a method is up to the vmod author::

	$Object foo(...)
	$Method .bar(...)
	$Method .foo(...)

Object instances are represented as pointers to vmod-implemented C
structs. Varnish only provides space to store the address of object
instances and ensures that the right object address gets passed to C
functions implementing methods.

	* Objects' scope and lifetime are the vcl

	* Objects can only be created in ``vcl_init {}`` and have
	  their destructors called by varnish after ``vcl_fini {}``
	  has completed.

vmod authors are advised to understand the prototypes in the
`vmodtool`\ -generated ``vcc_if.c`` file:

	* For ``$Object`` declarations, a constructor and destructor
	  function must be implemented

	* The constructor is named by the suffix ``__init``, always is
	  of ``VOID`` return type and has the following arguments
	  before the vcc-declared parameters:

	  * ``VRT_CTX`` as usual
	  * a pointer-pointer to return the address of the created
	    oject
	  * a string containing the vcl name of the object instance

	* The destructor is named by the suffix ``__fini``, always is
	  of ``VOID`` return type and has a single argument, the
	  pointer-pointer to the address of the object. The destructor
	  is expected clear the address of the object stored in that
	  pointer-pointer.

	* Methods gain the pointer to the object as an argument after
	   the ``VRT_CTX``.

As varnish is in no way involved in managing object instances other
than passing their addresses, vmods need to implement all aspects of
managing instances, in particular their memory management. As the
lifetime of object instances is the vcl, they will usually be
allocated from the heap.

Functions and Methods scope restriction
---------------------------------------

The ``$Restrict`` stanza offers a way to limit the scope of the preceding vmod function
or method, so that they can only be called from restricted vcl call sites.
It must only appear after a ``$Method`` or ``$Function`` and has the following syntax::

    $Restrict scope1 [scope2 ...]

Possible scope values are:
``backend, client, housekeeping, vcl_recv, vcl_pipe, vcl_pass, vcl_hash, vcl_purge, vcl_miss, vcl_hit,
vcl_deliver, vcl_synth, vcl_backend_fetch, vcl_backend_response, vcl_backend_error, vcl_init, vcl_fini``

Deprecated Aliases
------------------

The ``$Alias`` stanza offers a mechanism to rename a function or an
object's method without removing the previous name. This allows name
changes to maintain compatibility until the alias is dropped.

The syntax for a function is::

    $Alias deprecated_function original_function

    [description]

The syntax for a method is::

    $Alias .deprecated_method object.original_method

    [description]

The ``$Alias`` stanza can appear anywhere, this allows grouping them
in a dedicated "deprecated" section of their manual. The optional
description can be used to explain why a function was renamed.

.. _ref-vmod-vcl-c-types:

VCL and C data types
====================

VCL data types are targeted at the job, so for instance, we have data
types like "DURATION" and "HEADER", but they all have some kind of C
language representation.  Here is a description of them.

All but the PRIV types have typedefs: VCL_INT, VCL_REAL, etc.

Notice that most of the non-native (C pointer) types are ``const``,
which, if returned by a vmod function/method, are assumed to be
immutable. In other words, a vmod `must not` modify any data which was
previously returned.

When returning non-native values, the producing function is
responsible for arranging memory management.  Either by freeing the
structure later by whatever means available or by using storage
allocated from the client or backend workspaces.

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

BODY
	C-type: ``const void *``

	A type only used on the LHS of an assignment that can take
	either a blob or an expression that can be converted to a
	string.

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

	Enums will be passed as fixed pointers, so instead of string
	comparisons, also pointer comparisons with ``VENUM(name)`` are
	possible.

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

	A reference to a header object as ``req.http`` or ``bereq.http``.

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

REGEX
	C-type: ``const struct vre *``

	This is an opaque type for regular expressions with a VCL scope.
	The REGEX type is only meant for regular expression literals
	managed by the VCL compiler. For dynamic regular expressions or
	complex usage see the API from the ``include/vre.h`` file.

STRING
	C-type: ``const char *``

	A NUL-terminated text-string.

	Can be NULL to indicate a nonexistent string, for instance in::

		mymod.foo(req.http.foobar);

	If there were no "foobar" HTTP header, the vmod_foo()
	function would be passed a NULL pointer as argument.

STEVEDORE
	C-type: ``const struct stevedore *``

	A storage backend.

STRANDS
	C-Type: ``const struct strands *``

	Strands are a list of strings that gets passed in a struct with the
	following members:

	* ``int n``: the number of strings
	* ``const char **p``: the array of strings with `n` elements

	A VMOD should never hold onto strands beyond a function or method
	execution. See ``include/vrt.h`` for the details.

TIME
	C-type: ``double``

	Unit: seconds since UNIX epoch.

	An absolute time, as in 1284401161.

VCL_SUB
	C-type: ``const struct vcl_sub *``

	Opaque handle on a VCL subroutine.

	References to subroutines can be passed into VMODs as
	arguments and called later through ``VRT_call()``. The scope
	strictly is the VCL: vmods must ensure that ``VCL_SUB``
	references never be called from a different VCL.

	``VRT_call()`` fails the VCL for recursive calls and when the
	``VCL_SUB`` cannot be called from the current context
	(e.g. calling a subroutine accessing ``req`` from the backend
	side).

	For more than one invocation of ``VRT_call()``, VMODs *must*
	check if ``VRT_handled()`` returns non-zero inbetween calls:
	The called SUB may have returned with an action (any
	``return(x)`` other than plain ``return``) or may have failed
	the VCL, and in both cases the calling VMOD *must* return
	also, possibly after having conducted some cleanup. Note that
	undoing the handling through ``VRT_handling()`` is a bug.

	``VRT_check_call()`` can be used to check if a ``VRT_call()``
	would succeed in order to avoid the potential VCL failure.  It
	returns ``NULL`` if ``VRT_call()`` would make the call or an
	error string why not.

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
  compiled regular expression specific to a regsub() statement or
  simply caching the most recent output of some expensive operation.
  These private pointers live for the duration of the loaded VCL.

* ``PRIV_TASK`` "per task" private pointers are useful for state that
  applies to calls for either a specific request or a backend
  request. For instance this can be the result of a parsed cookie
  specific to a client. Note that ``PRIV_TASK`` contexts are separate
  for the client side and the backend side, so use in
  ``vcl_backend_*`` will yield a different private pointer from the
  one used on the client side.
  These private pointers live only for the duration of their task.

* ``PRIV_TOP`` "per top-request" private pointers live for the
  duration of one request and all its ESI-includes. They are only
  defined for the client side. When used from backend VCL subs, a NULL
  pointer will potentially be passed and a VCL failure triggered.
  These private pointers live only for the duration of their top
  level request

  .. PRIV_TOP see #3498

* ``PRIV_VCL`` "per vcl" private pointers are useful for such global
  state that applies to all calls in this VCL, for instance flags that
  determine if regular expressions are case-sensitive in this vmod or
  similar. The ``PRIV_VCL`` object is the same object that is passed
  to the VMOD's event function.
  This private pointer lives for the duration of the loaded VCL.

  The ``PRIV_CALL`` vmod_privs are finalized before ``PRIV_VCL``.

The way it works in the vmod code, is that a ``struct vmod_priv *`` is
passed to the functions where one of the ``PRIV_*`` argument types is
specified.

This structure contains three members::

	struct vmod_priv {
		void				*priv;
		long				len;
		const struct vmod_priv_methods  *methods;
	};

The ``.priv`` and ``.len`` elements can be used for whatever the vmod
code wants to use them for.

``.methods`` can be an optional pointer to a struct of callbacks::

	typedef void vmod_priv_fini_f(VRT_CTX, void *);

	struct vmod_priv_methods {
		unsigned			magic;
		const char			*type;
		vmod_priv_fini_f		*fini;
	};

``.magic`` has to be initialized to
``VMOD_PRIV_METHODS_MAGIC``. ``.type`` should be a descriptive name to
help debugging.

``.fini`` will be called for a non-NULL ``.priv`` of the ``struct
vmod_priv`` when the scope ends with that ``.priv`` pointer as its
second argument besides a ``VRT_CTX``.

The common case where a private data structure is allocated with
malloc(3) would look like this::

	static void
	myfree(VRT_CTX, void *p)
	{
		CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
		free (p);
	}

	static const struct vmod_priv_methods mymethods[1] = {{
		.magic = VMOD_PRIV_METHODS_MAGIC,
		.type = "mystate",
		.fini = myfree
	}};

	// ....

	if (priv->priv == NULL) {
		priv->priv = calloc(1, sizeof(struct myfoo));
		AN(priv->priv);
		priv->methods = mymethods;
		mystate = priv->priv;
		mystate->foo = 21;
		...
	} else {
		mystate = priv->priv;
	}
	if (foo > 25) {
		...
	}

Private Pointers Memory Management
----------------------------------

The generic malloc(3) / free(3) approach documented above works for
all private pointers. It is the simplest and less error prone (as long
as allocated memory is properly freed though the fini callback), but
comes at the cost of calling into the heap memory allocator.

Per-vmod constant data structures can be assigned to any private
pointer type, but, obviously, free(3) must not be used on them.

Dynamic data stored in ``PRIV_TASK`` and ``PRIV_TOP`` pointers can
also come from the workspace:

* For ``PRIV_TASK``, any allocation from ``ctx->ws`` works, like so::

	if (priv->priv == NULL) {
		priv->priv = WS_Alloc(ctx->ws, sizeof(struct myfoo));
		if (priv->priv == NULL) {
			VRT_fail(ctx, "WS_Alloc failed");
			return (...);
		}
		priv->methods = mymethods;
		mystate = priv->priv;
		mystate->foo = 21;
		...

* For ``PRIV_TOP``, first of all keep in mind that it must only be
  used from the client context, so vmod code should error out for
  ``ctx->req == NULL``.

  For dynamic data, the *top request's* workspace must be used, which
  complicates things a bit::

	if (priv->priv == NULL) {
		struct ws *ws;

		CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
		CHECK_OBJ_NOTNULL(ctx->req->top, REQTOP_MAGIC);
		CHECK_OBJ_NOTNULL(ctx->req->top->topreq, REQ_MAGIC);
		ws = ctx->req->top->topreq->ws;

		priv->priv = WS_Alloc(ws, sizeof(struct myfoo));
		// ... same as above for PRIV_TASK

Notice that allocations on the workspace do not need to be freed,
their lifetime is the respective task.

Private Pointers and Objects
----------------------------

``PRIV_TASK`` and ``PRIV_TOP`` arguments to methods are not per object
instance, but per vmod as for ordinary vmod functions. Thus, vmods
requiring per-task / per top-request state for object instances need
to implement other means to associate storage with object instances.

This is what ``VRT_priv_task()`` / ``VRT_priv_task_get()`` and
``VRT_priv_top()`` / ``VRT_priv_top_get()`` are for:

The non-get functions either return an existing ``PRIV_TASK`` /
``PRIV_TOP`` for a given ``void *`` argument or create one. They
return ``NULL`` in case of an allocation failure.

The ``_get()`` functions do not create a ``PRIV_*``, but return either
an existing one or ``NULL``.

By convention, private pointers for object instance are created on the
address of the object, as in this example for a ``PRIV_TASK``::

  VCL_VOID
  myvmod_obj_method(VRT_CTX, struct myvmod_obj *o)
  {
      struct vmod_priv *p;

      p = VRT_priv_task(ctx, o);

      // ... see above

The ``PRIV_TOP`` case looks identical except for calling
``VRT_priv_top(ctx, o)`` in place of ``VRT_priv_task(ctx, o)``, but be
reminded that the ``VRT_priv_top*()`` functions must only be called
from client context (if ``ctx->req != NULL``).

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
reference by calling ``VRT_VCL_Prevent_Discard`` when you receive a ``VCL_EVENT_WARM`` and
later calling ``VRT_VCL_Allow_Discard`` once the background job is over. Receiving a
``VCL_EVENT_COLD`` is your cue to terminate any background job bound to a VCL.

You can find an example of VCL references in vmod-debug::

	priv_vcl->vclref = VRT_VCL_Prevent_Discard(ctx, "vmod-debug");
	...
	VRT_VCL_Allow_Discard(&ctx, &priv_vcl->vclref);

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

Statistics Counters
===================

Starting in Varnish 6.0, VMODs can define their own counters that appear
in *varnishstat*.

If you're using autotools, see the ``VARNISH_COUNTERS`` macro in
varnish.m4 for documentation on getting your build set up.

Counters are defined in a .vsc file. The ``VARNISH_COUNTERS`` macro
calls *vsctool.py* to turn a *foo.vsc* file into *VSC_foo.c* and
*VSC_foo.h* files, just like *vmodtool.py* turns *foo.vcc* into
*vcc_foo_if.c* and *vcc_foo_if.h* files. Similarly to the VCC files, the
generated VSC files give you a structure and functions that you can use
in your VMOD's code to create and destroy the counters your defined. The
*vsctool.py* tool also generates a *VSC_foo.rst* file that you can
include in your documentation to describe the counters your VMOD has.

The .vsc file looks like this:

.. code-block:: none

	.. varnish_vsc_begin:: xkey
		:oneliner:	xkey Counters
		:order:		70

		Metrics from vmod_xkey

	.. varnish_vsc:: g_keys
		:type:		gauge
		:oneliner:	Number of surrogate keys

		Number of surrogate keys in use. Increases after a request that includes a new key in the xkey header. Decreases when a key is purged or when all cache objects associated with a key expire.

	.. varnish_vsc_end:: xkey

Counters can have the following parameters:

type
	The type of metric this is. Can be one of ``counter``,
	``gauge``, or ``bitmap``.

ctype
	The type that this counter will have in the C code. This can
	only be ``uint64_t`` and does not need to be specified.

level
	The verbosity level of this counter. *varnishstat* will only
	show counters with a higher verbosity level than the one
	currently configured. Can be one of ``info``, ``diag``, or
	``debug``.

oneliner
	A short, one line description of the counter.

group
	I don't know what this does.

format
	Can be one of ``integer``, ``bytes``, ``bitmap``, or ``duration``.

After these parameters, a counter can have a longer description, though
this description has to be all on one line in the .vsc file.

You should call ``VSC_*_New()`` when your VMOD is loaded and
``VSC_*_Destroy()`` when it is unloaded. See the generated
``VSC_*.h`` file for the full details about the structure that contains
your counters.

Temporary Files
===============

``varnishd`` creates a directory named ``worker_tmpdir`` under the
varnish working directory (see ``varnishd -n`` argument) for
read/write access by the worker process.

From the perspective of VMODs, the relative path is always
``worker_tmpdir``.

This directory is intended (though not limited) to provide a place for
VMODs to create temporary files using ``mkstemp()`` and related libc
functions. VMODs are responsible for cleaning up files which are no
longer required, and they will ultimately be removed when the
``varnishd`` worker process restarts. There is no isolation between
VMODs (as is the case anyway).

A simple example for how to use it::

	#include <stdlib.h>
	#include <unistd.h>

	#include "vdef.h"
	#include "vas.h"

	static void
	tmpfile_example(void) {
	    int fd;
	    char name[] = "worker_tmpdir/myvmod.XXXXXX";

	    fd = mkstemp(name);
	    if (fd < 0) {
		// handle error
		return;
	    }

	    // hide file
	    AZ(unlink(name));

	    // use fd

	    AZ(close(fd));
	}
