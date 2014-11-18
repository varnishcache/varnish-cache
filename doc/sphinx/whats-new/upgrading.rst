.. _whatsnew_upgrading:

%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish 4
%%%%%%%%%%%%%%%%%%%%%%

Changes to VCL
==============

The backend fetch parts of VCL have changed in Varnish 4. We've tried to
compile a list of changes needed to upgrade here.

Version statement
~~~~~~~~~~~~~~~~~

To make sure that people have upgraded their VCL to the current
version, Varnish now requires the first line of VCL to indicate the
VCL version number::

    vcl 4.0;

req.request is now req.method
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To align better with RFC naming, `req.request` has been renamed to
`req.method`.

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

vcl_error is now vcl_backend_error
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To make a distinction between internally generated errors and
VCL synthetic responses, `vcl_backend_error` will be called when
varnish encounters an error when trying to fetch an object.

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

obj in vcl_error replaced by beresp in vcl_backend_error
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To better represent a the context in which it is called, you
should now use `beresp.*` vcl_backend_error, where you used to
use `obj.*` in `vcl_error`.

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

req.* not available in vcl_backend_response
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

req.* used to be available in `vcl_fetch`, but after the split of
functionality, you only have 'bereq.*' in `vcl_backend_response`.

vcl_* reserved
~~~~~~~~~~~~~~

Any custom-made subs cannot be named 'vcl_*' anymore. This namespace
is reserved for builtin subs.

req.backend.healthy replaced by std.healthy(req.backend_hint)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Remember to import the std module if you're not doing so already.

client.port, and server.port replaced by respectively std.port(client.ip) and std.port(server.ip)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

`client.ip` and `server.ip` are now proper datatypes, which renders
as an IP address by default. You need to use the `std.port()`
function to get the port number.

Invalidation with purge
~~~~~~~~~~~~~~~~~~~~~~~

Cache invalidation with purges is now done via `return(purge)` from `vcl_recv`.
The `purge;` keyword has been retired.

obj is now read-only
~~~~~~~~~~~~~~~~~~~~

`obj` is now read-only.  `obj.last_use` has been retired.

Some return values have been replaced
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Apart from the new `synth` return value described above, the
following has changed:

 - `vcl_recv` must now return `hash` instead of `lookup`
 - `vcl_hash` must now return `lookup` instead of `hash`
 - `vcl_pass` must now return `fetch` instead of `pass`


Backend restarts are now retry
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In 3.0 it was possible to do `return(restart)` after noticing that
the backend response was wrong, to change to a different backend.

This is now called `return(retry)`, and jumps back up to `vcl_backend_fetch`.

This only influences the backend fetch thread, client-side handling is not affected.


default/builtin VCL changes
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The VCL code that is appended to user-configured VCL automatically
is now called the builtin VCL. (previously default.vcl)

The builtin VCL now honors Cache-Control: no-cache (and friends)
to indicate uncacheable content from the backend.


The `remove` keyword is gone
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Replaced by `unset`.


X-Forwarded-For is now set before vcl_recv
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In many cases, people unintentionally removed X-Forwarded-For when
implementing their own vcl_recv. Therefore it has been moved to before
vcl_recv, so if you don't want an IP added to it, you should remove it
in vcl_recv.


Changes to existing parameters
==============================

session_linger
~~~~~~~~~~~~~~
`session_linger` has been renamed to `timeout_linger` and it is in
seconds now (previously was milliseconds).

sess_timeout
~~~~~~~~~~~~
`sess_timeout` has been renamed to `timeout_idle`.

sess_workspace
~~~~~~~~~~~~~~

In 3.0 it was often necessary to increase `sess_workspace` if a
lot of VMODs, complex header operations or ESI were in use.

This is no longer necessary, because ESI scratch space happens
elsewhere in 4.0.

If you are using a lot of VMODs,  you may need to increase
either `workspace_backend` and `workspace_client` based on where
your VMOD is doing its work.

thread_pool_purge_delay
~~~~~~~~~~~~~~~~~~~~~~~
`thread_pool_purge_delay` has been renamed to `thread_pool_destroy_delay`
and it is in seconds now (previously was milliseconds).

thread_pool_add_delay and thread_pool_fail_delay
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
They are in seconds now (previously were milliseconds).

New parameters since 3.0
========================

vcc_allow_inline_c
~~~~~~~~~~~~~~~~~~

You can now completely disable inline C in your VCL, and it is
disabled by default.

Other changes
=============

New log filtering
~~~~~~~~~~~~~~~~~

The logging framework has a new filtering language, which means
that the -m switch has been replaced with a new -q switch.
See :ref:`ref-vsl-query` for more information about the new
query language.
