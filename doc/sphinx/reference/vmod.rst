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
and we will put "butique" functions in it, such as the "toupper"
function shown above.  The full contents of the "std" module is
documented in XXX:TBW.

This part of the manual is about how you go about writing your own
VMOD, how the language interface between C and VCC works etc.  This
explanation will use the "std" VMOD as example, having a varnish
source tree handy may be a good idea.

The vmod.vcc file
=================

The interface between your VMOD and the VCL compiler ("VCC") and the
VCL runtime ("VRT") is defined in the vmod.vcc file which a python
script called "vmod.py" turns into thaumathurgically challenged C
data structures that does all the hard work.

The std VMODs vmod.vcc file looks somewhat like this::

	Module std
	Meta meta_function
	Function STRING toupper(STRING_LIST)
	Function STRING tolower(PRIV_VCL, STRING_LIST)
	Function VOID set_ip_tos(INT)

The first line gives the name of the module, nothing special there.

The second line specifies an optional "Meta" function, which will
be called whenever a VCL program which imports this VMOD is loaded
or unloaded.  You probably will not need such a function, so we will
postpone that subject until further down.

The next three lines specify two functions in the VMOD, along with the
types of the arguments, and that is probably where the hardest bit
of writing a VMOD is to be found, so we will talk about that at length
in a moment.

Notice that the third function returns VOID, that makes it a "procedure"
in VCL lingo, meaning that it cannot be used in expressions, right
side of assignments and such places.  Instead it can be used as a
primary action, something functions which return a value can not::

	sub vcl_recv {
		std.set_ip_tos(32);
	}

Running vmod.py on the vmod.vcc file, produces an "vcc_if.c" and
"vcc_if.h" files, which you must use to build your shared library
file.

Forget about vcc_if.c everywhere but your Makefile, you will never
need to care about its contents, and you should certainly never
modify it, that voids your warranty instantly.

But vcc_if.h is important for you, it contains the prototypes for
the functions you want to export to VCL.

For the std VMOD, the compiled vcc_if.h file looks like this::

	struct sess;
	struct VCL_conf;
	const char * vmod_toupper(struct sess *, const char *, ...);
	const char * vmod_tolower(struct sess *, void **, const char *, ...);
	int meta_function(void **, const struct VCL_conf *);

Those are your C prototypes.  Notice the "vmod\_" prefix on the function
names and the C-types as return types and arguments.

VCL and C data types
====================

VCL data types are targeted at the job, so for instance, we have data
types like "DURATION" and "HEADER", but they all have some kind of C
language representation.  Here is a description of them, from simple
to nasty.

INT
	C-type: int

	An integer as we know and love them.

REAL
	C-type: double

	A floating point value

DURATION
	C-type: double

	Units: seconds

	A time interval, as in "25 minutes".

TIME
	C-type: double

	Units: seconds since UNIX epoch

	An absolute time, as in "Mon Sep 13 19:06:01 UTC 2010".

STRING
	C-type: const char *

	A NUL-terminated text-string.

	Can be NULL to indicate that the nonexistent string, for
	instance:

		mymod.foo(req.http.foobar);

	If there were no "foobar" HTTP header, the vmod_foo()
	function would be passed a NULL pointer as argument.

	When used as a return value, the producing function is
	responsible for arranging memory management.  Either by
	freeing the string later by whatever means available or
	by using storage allocated from the session or worker
	workspaces.

STRING_LIST
	C-type: const char *, ...

	A multi-component text-string.  We try very hard to avoid
	doing text-processing in Varnish, and this is one way we
	do that, by not editing separate pieces of a sting together
	to one string, until we need to.

	Consider this contrived example::

		set bereq.http.foo = std.toupper(req.http.foo + req.http.bar);

	The usual way to do this, would be be to allocate memory for
	the concatenated string, then pass that to toupper() which in
	turn would return another freshly allocated string with the
	modified result.  Remember: strings in VCL are "const", we
	cannot just modify the string in place.

	What we do instead, is declare that toupper() takes a "STRING_LIST"
	as argument.  This makes the C function implementing toupper()
	a vararg function (see the prototype above) and responsible for
	considering all the "const char *" arguments it finds, until the
	magic marker "vrt_magic_string_end" is encountered.

	Bear in mind that the individual strings in a STRING_LIST can be
	NULL, as described under STRING, that is why we do not use NULL
	as the terminator.

	Right now we only support STRING_LIST being the last argument to
	a function, we may relax that at a latter time.

	If you don't want to bother with STRING_LIST, just use STRING
	and make sure your sess_workspace param is big enough.

PRIV_VCL
	C-type: void **

	Passes a pointer to a per-VCL program private "void *" for
	this module.
	
	This is where the Meta function comes into the picture.

	Each VCL program which imports a given module can provide the
	module with a pointer to hang private data from.

	When the VCL program is loaded, the Meta function will be
	called with the private pointer and with the VCL programs
	descriptor structure as second argument, to give the module
	a chance to initialize things.

	When the VCL program is discarded, the Meta function will
	also be called, but this time with a second argument of NULL,
	to give the module a chance to clean up and free per VCL stuff.

	When the last VCL program that uses the module is discarded
	the shared library containing the module will be dlclosed().

VOID
	C-type: void

	Can only be used for return-value, which makes the function a VCL
	procedure.

IP, BOOL, HEADER
	XXX: these types are not released for use in vmods yet.

