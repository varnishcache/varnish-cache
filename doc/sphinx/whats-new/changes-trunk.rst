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

Some parameters have dependencies and those are better documented now. For
example :ref:`ref_param_thread_pool_min` can't be increased above
:ref:`ref_param_thread_pool_max`, which is now indicated as such in the
manual.

On a running Varnish instance the ``param.show`` command will display the
actual minimum or maximum, but an attempt to ``param.set`` a parameter above
or below its dynamic maximum or minimum will mention the failure's cause in
the error message::

    varnish> param.show thread_pool_reserve
    200
    thread_pool_reserve
            Value is: 0 [threads] (default)
            Maximum is: 95

            [...]

    varnish> param.set thread_pool_reserve 100
    106
    Must be no more than 95 (95% of thread_pool_min)

    (attempting to set param 'thread_pool_reserve' to '100')

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

VMODs
=====

**XXX changes in the bundled VMODs**

varnishlog
==========

**XXX changes concerning varnishlog(1) and/or vsl(7)**

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
