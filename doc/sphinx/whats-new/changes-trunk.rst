**Note: This is a working document for a future release, with running
updates for changes in the development branch. For changes in the
released versions of Varnish, see:** :ref:`whats-new-index`

.. _whatsnew_changes_CURRENT:

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
Changes in Varnish **$NEXT_RELEASE**
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

For information about updating your current Varnish deployment to the
new version, see :ref:`whatsnew_upgrading_CURRENT`.

A more detailed and technical account of changes in Varnish, with
links to issues that have been fixed and pull requests that have been
merged, may be found in the `change log`_.

.. _change log: https://github.com/varnishcache/varnish-cache/blob/master/doc/changes.rst

varnishd
========

Parameters
~~~~~~~~~~

**XXX changes in -p parameters**

Other changes in varnishd
~~~~~~~~~~~~~~~~~~~~~~~~~

Changes to VCL
==============

VCL variables
~~~~~~~~~~~~~

**XXX new, deprecated or removed variables, or changed semantics**

Other changes to VCL
~~~~~~~~~~~~~~~~~~~~

**TODO: return (error);**

VMODs
=====

**XXX changes in the bundled VMODs**

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

varnishadm
==========

**XXX changes concerning varnishadm(1) and/or varnish-cli(7)**

varnishstat
===========

**XXX changes concerning varnishstat(1) and/or varnish-counters(7)**

varnishtest
===========

**XXX changes concerning varnishtest(1) and/or vtc(7)**

Changes for developers and VMOD authors
=======================================

**XXX changes concerning VRT, the public APIs, source code organization,
builds etc.**

*eof*
