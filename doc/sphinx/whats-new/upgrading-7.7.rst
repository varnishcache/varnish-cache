.. _whatsnew_upgrading_7.7:

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish-Cache 7.7
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

In general, upgrading from Varnish 7.6 to 7.7 should not require any changes
besides the actual upgrade.

Note, however, that some log messages and in particular timestamps have changed,
see :ref:`whatsnew_changes_7.7` and
:ref:`whatsnew_changes_7.7_h2_timestamps` in particular. Here, we only
summarize the changes:

* We have changed how http/2 timestamps are taken.

* Details of http/2 related log entries have changed.

* The ``varnishncsa`` format ``Varnish:handling`` now also outputs ``hitmiss``
  and ``hitpass``.

* ``varnishncsa`` now outputs headers as they are received and sent.

Upgrade notes for VMOD developers
=================================

``vmodtool.py`` now creates a file ``vmod_vcs_version.txt`` in the current
working directory when called from a git tree. This file is intended to
transport version control system information to builds from distribution
bundles.

VMOD authors should add it to the distribution and otherwise ignore it for SCM.

Where git and automake are used, this can be accomplished by adding
``vmod_vcs_version.txt`` to the ``.gitignore`` file and to the ``EXTRA_DIST``
and ``DISTCLEANFILES`` variables in ``Makefile.am``.

If neither git is used nor ``vmod_vcs_version.txt`` present, ``vmodtool.py``
will add ``NOGIT`` to the vmod as the vcs identifier.


*eof*
