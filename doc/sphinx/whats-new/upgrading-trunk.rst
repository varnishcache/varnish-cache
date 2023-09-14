**Note: This is a working document for a future release, with running
updates for changes in the development branch. For changes in the
released versions of Varnish, see:** :ref:`whats-new-index`

.. _whatsnew_upgrading_CURRENT:

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish **$NEXT_RELEASE**
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

Important VCL Changes
=====================

When upgrading from Varnish-Cache 7.3, there is only one breaking
change to consider in VCL:

The ``Content-Length`` and ``Transfer-Encoding`` headers are now
*protected*, they can neither be changed nor unset. This change was
implemented to avoid de-sync issues from accidental, inadequate
modifications of these headers.

For the common use case of ``unset (be)req.http.Content-Length`` to
dismiss a request body, ``unset (be)req.body`` should be used.

Parameter Changes
=================

The new ``varnishd`` parameter ``startup_timeout`` now specifically
replaces ``cli_timeout`` for the initial startup only. In cases where
``cli_timeout`` was increased specifically to accommodate long startup
times (e.g. for storage engine initialization), ``startup_timeout``
should be used.

*eof*
