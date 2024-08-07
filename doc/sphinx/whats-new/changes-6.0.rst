..
	Copyright (c) 2018 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _whatsnew_changes_6.0:

Changes in Varnish 6.0
======================

Usually when we do dot-zero releases in Varnish, it means that
users are in for a bit of work to upgrade to the new version,
but 6.0 is actually not that scary, because most of the changes
are either under the hood or entirely new features.

The biggest user-visible change is probably that we, or to be totally
honest here: Geoff Simmons (UPLEX), have added support for Unix Domain
Sockets, both :ref:`for clients <upd_6_0_uds_acceptor>` and for
:ref:`backend servers <upd_6_0_uds_backend>`.

Because UNIX Domain Sockets have nothing like IP numbers, we were
forced to define a new level of the VCL language ``vcl 4.1`` to
cope with UDS.

Both ``vcl 4.0`` and ``vcl 4.1`` are supported, and it is the primary
source-file which controls which it will be, and you can ``include``
lower versions, but not higher versions than that.

Some old variables are not available in 4.1 and some new variables
are not available in 4.0.  Please see :ref:`vcl_variables` for
specifics.

There are a few other changes to the ``vcl 4.0``, most notably that
we now consider upper- and lower-case the same for symbols.

The HTTP/2 code has received a lot of attention from Dag Haavi
Finstad (Varnish Software) and it holds up in production on several
large sites now.

There are new and improved VMODs:

* :ref:`vmod_directors(3)` -- Much work on the ``shard`` director

* :ref:`vmod_proxy(3)` -- Proxy protocol information

* :ref:`vmod_unix(3)` -- Unix Domain Socket information

* :ref:`vmod_vtc(3)` -- Utility functions for writing :ref:`varnishtest(1)` cases.

The ``umem`` stevedore has been brought back on Solaris
and it is the default storage method there now.

More error situations now get vcl ``failure`` handling,
this should make life simpler for everybody we hope.

And it goes without saying that we have fixed a lot of bugs too.


Under the hood (mostly for developers)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The big thing is that the ``$Abi [vrt|strict]`` should now
have settled.  We have removed all the stuff from ``<cache.h>``
which is not available under ``$Abi vrt``, and this hopefully
means that VMODS will work without recompilation on several
subsequent varnish versions.  (There are some stuff related
to packaging which takes advantage of this, but it didn't
get into this release.)

VMODS can define their own stats counters now, and they work
just like builtin counters, because there is no difference.

The counters are described in a ``.vsc`` file which is
processed with a new python script which does a lot of
magic etc.  There is a tiny example in ``vmod_debug`` in
the source tree.  If you're using autotools, a new
``VARNISH_COUNTERS`` macro helps you set everything up,
and is documented in ``varnish.m4``.

This took a major retooling of the stats counters in general, and
the VSM, VSC and VSL apis have all subtly or not so subtly changed
as a result.

VMOD functions can take optional arguments, these are different
from defaulted arguments in that a separate flag tells if they
were specified or not in the call.  For reasons of everybody's
sanity, all the arguments gets wrapped in a function-specific
structure when this is used.

The ``vmodtool.py`` script has learned other new tricks, and
as a result also produces nicer ``.rst`` output.

VCL types ``INT`` and ``BYTES`` are now 64bits on all platforms.

VCL ENUM have gotten a new implementation, so the pointers
are now constant and can be compared as such, rather than
with ``strcmp(3)``.

We have a new type of ``binary`` VSL records which are hexdumped
by default, but on the API side, rather than in ``varnishd``.
This saves both VSL bandwidth and processing power, as they are
usually only used for deep debugging and mostly turned off.

The ``VCC`` compilers has received a lot of work in two areas:

The symbol table has been totally revamped to make it ready for
variant symbols, presently symbols which are different in
``vcl 4.0`` and ``vcl 4.1``.

The "prototype" information in the VMOD shared library has been
changed to JSON, (look in your vcc_if.c file if you don't believe
me), and this can express more detailed information, presently
the optional arguments.

The stuff only we care about
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Varnishtest's ``process`` has grown ``pty(4)`` support, so that
we can test curses-based programs like our own utilities.

This has (finally!) pushed our code coverage, across all the
source code in the project up to 90%.

We have also decided to make our python scripts PEP8 compliant, and
``vmodtool.py`` is already be there.

The VCL variables are now defined in the ``.rst`` file, rather
than the other way around, this makes the documentation better
at the cost of minor python-script complexity.

We now produce weekly snapshots from ``-trunk``, this makes it
easier for people to test all the new stuff.

We have not quite gotten the half-yearly release-procedure under
control.

I'm writing this the evening before the release, trying to squeeze
out of my brain what I should have written here long time ago,
and we have had far more commits this last week than is reasonable.

But we *have* gotten better at it.

Really!

*eof*
