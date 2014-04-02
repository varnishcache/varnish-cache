.. _whatsnew_upgrading:

%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish 4
%%%%%%%%%%%%%%%%%%%%%%

Changes to VCL
==============

Much of the VCL syntax has changed in Varnish 4. We've tried to
compile a list of changes needed to upgrade here.

Version statement
~~~~~~~~~~~~~~~~~

To make sure that people have upgraded their VCL to the current
version, Varnish now requires the first line of VCL to indicate the
VCL version number::

    vcl 4.0;

vcl_fetch is now vcl_backend_response
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Directors have been moved to the vmod_directors
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To make directors (backend selection logic) easier to extend, the
directors are now defined in loadable VMODs.

Setting a backend for future fetches in `vcl_recv` is now done as follows::

    sub vcl_init {
        new cluster1 = directors.round_robin();
        cluster1.add_backend(b1, 1.0);
        cluster1.add_backend(b2, 1.0);
    }

    sub vcl_recv {
        set req.backend_hint = cluster1.backend();
    }

Note the extra `.backend()` needed after the director name.

Use the hash director as a client director
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Since the client director was already a special case of the hash director, it
has been removed, and you should use the hash director directly::

    sub vcl_init {
        new h = directors.hash();
        h.add_backend(b1, 1);
        h.add_backend(b2, 1);
    }

    sub vcl_recv {
        set req.backend_hint = h.backend(client.identity);
    }

error() is now synth()
~~~~~~~~~~~~~~~~~~~~~~

And you must explicitly return it::

    return (synth(999, "Response"));

Synthetic responses in vcl_synth
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Setting headers on synthetic response bodies made in vcl_synth are now done on
resp.http instead of obj.http.

The synthetic keyword is now a function::

    if (resp.status == 799) {
        set resp.status = 200;
        set resp.http.Content-Type = "text/plain; charset=utf-8";
        synthetic("You are " + client.ip);
        return (deliver);
    }

hit_for_pass objects are created using beresp.uncacheable
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Example::

    sub vcl_backend_response {
        if (beresp.http.X-No-Cache) {
            set beresp.uncacheable = true;
            set beresp.ttl = 120s;
            return (deliver);
        }
    }

vcl_recv should return(hash) instead of lookup now
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

req.* not available in vcl_backend_response
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

req.* used to be available in `vcl_fetch`, but after the split of
functionality, you only have 'bereq.*' in `vcl_backend_response`.

vcl_* reserved
~~~~~~~~~~~~~~

Any custom-made subs cannot be named 'vcl_*' anymore. This namespace
is reserved for builtin subs.

req.backend.healthy replaced by std.healthy(req.backend)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

obj is now read-only
~~~~~~~~~~~~~~~~~~~~

`obj` is now read-only. `obj.hits`, if enabled in VCL, now counts per
objecthead, not per object. `obj.last_use` has been retired.


default/builtin VCL changes
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The VCL code that is appended to user-configured VCL automatically
is now called the builtin VCL. (previously default.vcl)

The builtin VCL now honors Cache-Control: no-cache (and friends)
to indicate uncacheable content from the backend.


The `remove` keyword is gone
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Replaced by `unset`.

Changes to parameters
=====================

linger
~~~~~~

sess_timeout
~~~~~~~~~~~~

sess_workspace
~~~~~~~~~~~~~~

In 3.0 it was often necessary to increase `sess_workspace` if a
lot of VMODs, complex header operations or ESI were in use.

This memory segment has been split into two in 4.0;
`workspace_backend` and `workspace_client`.

In most cases where you increased `sess_workspace` before, you
want to increase `workspace_client` now.

vcc_allow_inline_c
~~~~~~~~~~~~~~~~~~

This parameter is new since 3.0, and prohibits the use of inline
C code in VCL by default.
