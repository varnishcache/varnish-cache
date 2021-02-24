..
	Copyright (c) 2020 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _whatsnew_changes_6.3:

%%%%%%%%%%%%%%%%%%%%%%
Changes in Varnish 6.3
%%%%%%%%%%%%%%%%%%%%%%

For information about updating your current Varnish deployment to the
new version, see :ref:`whatsnew_upgrading_6.3`.

A more detailed and technical account of changes in Varnish, with
links to issues that have been fixed and pull requests that have been
merged, may be found in the `change log`_.

.. _change log: https://github.com/varnishcache/varnish-cache/blob/master/doc/changes.rst

varnishd
========

Parameters
~~~~~~~~~~

A new :ref:`ref_param_pipe_sess_max` parameter allows to limit the number of
concurrent pipe transactions. The default value is zero and means unlimited,
for backwards compatibility.

Other changes in varnishd
~~~~~~~~~~~~~~~~~~~~~~~~~

The transferred bytes accounting for HTTP/2 sessions is more accurate:
``ReqAcct`` log records no longer report a full delivery if a stream did
not complete.

The meaning of VCL temperature changed for the ``auto`` state: it used to
automatically cool down a VCL transitioning from active to available, but
that VCL would remain ``cold``. It now works in both directions, and such a
cold VCL keeps the ``auto`` state and may be used or labelled immediately
without an explicit change of state to ``warm``.

As a result, a VCL with the ``cold`` state will no longer warm up
automatically.

The management of counters, and in particular dynamic counters (for example
appearing or disappearing when a VCL is loaded or discarded), has seen
significant performance improvements and setups involving a large amount of
backends should be more responsive.

Changes to VCL
==============

VCL variables
~~~~~~~~~~~~~

The :ref:`ref_param_timeout_idle` parameter can be overriden in VCL using the
``sess.timeout_idle`` variable.

Other changes to VCL
~~~~~~~~~~~~~~~~~~~~

A new ``error`` transition to ``vcl_backend_error`` allows to purposely move
to that subroutine. It is similar to the ``synth`` transition and can
optionally take arguments. The three following statements are equivalent::

    return (error);
    return (error(503));
    return (error(503, "Service Unavailable"));

The ``error`` transition is available in :ref:`vcl_backend_fetch` and
:ref:`vcl_backend_response`. Using an explicit ``error`` transition in
``vcl_backend_fetch`` does not increment the ``MAIN.fetch_failed`` counter.

It is possible to import the same VMOD twice, as long as the two imports are
identical and wouldn't otherwise conflict. This allows for example included
VCL files to import the VMODs they need without preventing the including VCL
to also perform the same import.

Similarly, it is now possible to include a VMOD under a different name::

    import directors as dir;

    sub vcl_init {
        new rr = dir.round_robin();
    }

This can be useful for VMODs with a long name, or VMODs that could use a
more expressive name in VCL code.

The built-in VCL turns the ``Host`` header lowercase in ``vcl_recv`` to
improve its hashing later in ``vcl_hash`` since domain names are case
insensitive.

VMODs
=====

``std.ip()`` now takes an optional ``STRING`` argument to specify a port
number or service name.

See: :ref:`std.ip()`

vsl-query(7)
============

The syntax for VSL queries, mainly available via the ``-q`` option with
:ref:`varnishlog(1)` and similar tools, has slightly changed. Previously
and end of line in a query would be treated as a simple token separator
so in a script you could for example write this::

    varnishlog -q '
        tag operator operand or
        tag operator operand or
        tag operator operand
    ' -g request ...

From now on, a query ends at the end of the line, but multiple queries
can be specified in which case it acts as if the ``or`` operator was used
to join all the queries.

With this change in the syntax, the following query::

    varnishlog -q '
        query1
        query2
    '

is equivalent to::

    varnishlog -q '(query1) or (query2)'

In other words, if you are using a Varnish utility to process transactions
for several independent reasons, you can decompose complex queries into
simpler ones by breaking them into separate lines, and for the most complex
queries possibly getting rid of parenthesis you would have needed in a
single query.

If your query is complex and long, but cannot appropriately be broken down
into multiple queries, you can still break it down into multiple lines by
using a backslash-newline sequence::

    tag operator operand and \
    tag operator operand and \
    tag operator operand

See :ref:`vsl-query(7)` for more information about this change.

With this new meaning for an end of line in a query it is now possible to
add comments in a query. If you run into the situation where again you need
to capture transactions for multiple reasons, you may document it directly
in the query::

    varnishlog -q '
        # catch varnish errors
        *Error

        # catch client errors
        BerespStatus >= 400 and BerespStatus < 500

        # catch backend errors
        BerespStatus >= 500
    ' -g request

This way when you later revisit a complex query, comments may help you
maintain an understanding of each individual query. This can become even
more convenient when the query is stored in a file.

varnishlog(1), varnishncsa(1) and others
========================================

Our collection of log-processing tools gained the ability to specify
multiple ``-q`` options. While previously only the last ``-q`` option
would prevail you may now pass multiple individual queries and filtering
will operate as if the ``or`` operator was used to join all the queries.

A new ``-Q`` option allows you to read the query from a file instead. It
can also be used multiple times and adds up to any ``-q`` option specified.

Similar to ``-c`` or ``-b`` for client or backend transactions,
``varnishncsa(1)`` can take a ``-E`` option to include ESI transactions.

``BackendStart`` log records are no longer used, but newer versions of log
utilities should still recognize deprecated records. It remains possible
to read logs written to a file with an older version of ``varnishlog(1)``,
and that backward compatibility officially goes as far as Varnish 6.0.0
even though it *may* be possible to read logs saved from older releases.

``Debug`` records are no longer logged by default and can be removed from
the :ref:`ref_param_vsl_mask` parameter to appear in the logs. Since such
records are not meant for production they are only automatically enabled
by ``varnishtest(1)``.

varnishstat
===========

A new ``MAIN.n_pipe`` gauge keeps track of the number of ongoing pipe
transactions.

A new ``MAIN.pipe_limited`` counter keeps track of how many times a
transaction failed to turn into a pipe because of the
:ref:`ref_param_pipe_sess_max` parameter.

varnishtest
===========

A ``client`` can now use the ``-method`` action for ``txreq`` commands to
specify the request method. This used to be done with ``-req`` which remains
as an alias for compatibility.

A ``client`` or ``server`` may use the ``-bodyfrom`` action for respectively
``txreq`` or ``txresp`` commands to send a body from a file.

An HTTP/2 ``client`` or ``server`` can work with gzip content encoding and has
access to ``-gzipbody`` and ``-gziplen``.

Changes for developers and VMOD authors
=======================================

The most notable change for VMOD developers is the deprecation of string lists
in favor of strands.

As usual, new functions were added to VRT, and others were changed or removed.
See ``vrt.h`` for a list of changes since the 6.2.0 release.

We continue to remove functions from VRT that weren't meant to be used by VMOD
authors and were only part of the VMOD infrastructure code.

*eof*
