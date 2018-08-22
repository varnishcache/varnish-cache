.. _whatsnew_changes_6.1:

**NOTE: The present document is work in progress for the September
2018 release.** The version number 6.1.0 is provisional and may
change. See :ref:`whatsnew_changes_6.0` for notes about the currently
most recent Varnish release.

Changes in Varnish 6.1
======================

**XXX**

Varnish now won't rewrite the content-length header when responding to any HEAD
request, making it possible to cache HEAD requests independently from the GET
ones (peviously a HEAD request had to be a pass to avoid this rewriting).

*eof*
