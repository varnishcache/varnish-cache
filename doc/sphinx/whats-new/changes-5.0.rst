.. _whatsnew_changes_5.0:

Changes in Varnish 5.0
======================

Varnish 5.0 changes some (mostly) internal APIs and adds some major new
features over Varnish 4.1.


Separate VCL files and VCL labels
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Varnish 5.0 supports jumping from the active VCL's vcl_recv{} to
another VCL via a VCL label.

The major use of this will probably be to have a separate VCL for
each domain/vhost, in order to untangle complex VCL files, but
it is not limited to this criteria, it would also be possible to
send all POSTs, all JPEG images or all traffic from a certain
IP range to a separate VCL file.

VCL labels can also be used to give symbolic names to loaded VCL
configurations, so that operations personnel only need to know
about "normal", "weekend" and "emergency", and web developers
can update these as usual, without having to tell ops what the
new weekend VCL is called.


Very Experimental HTTP/2 support
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

We are in the process of adding HTTP/2 support to Varnish, but
the code is very green still - life happened.

But you can actually get a bit of traffic though it already, and
we hope to have it production ready for the next major release
(2017-03-15).

Varnish supports HTTP/1 -> 2 upgrade.  For political reasons,
no browsers support that, but tools like curl does.

For encrypted HTTP/2 traffic, put a SSL proxy in front of Varnish.

