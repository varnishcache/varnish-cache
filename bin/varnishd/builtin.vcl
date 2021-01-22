#-
# Copyright (c) 2006 Verdens Gang AS
# Copyright (c) 2006-2015 Varnish Software AS
# All rights reserved.
#
# Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.

## Built-in VCL
## ============
##
## The built-in VCL is always appended at the very end of a loaded VCL
## as if it had been included. It contains default rules for a sane HTTP
## cache and ensures that at least one return action is used per subroutine.
##
## There is one return action accessible almost universally from any context.
##
## .. _fail:
##
## ``fail``
## --------
##
##     Transition to :ref:`vcl_synth` on the client side as for
##     ``return(synth(503, "VCL Failed"))``, but with any request state
##     changes undone as if ``std.rollback()`` was called and forcing a
##     connection close.
##
##     Intended for fatal errors, for which only minimal error handling is
##     possible.
##
vcl 4.0;

## Client Side
## -----------
##
## The following subroutines are part of the client state machine, they
## manage the lifecycle of client requests and responses. There are common
## actions shared by multiple client subroutines.
##
## .. _synth:
##
## ``synth(status code, reason)``
## ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
##
##     Transition to :ref:`vcl_synth` with ``resp.status`` and
##     ``resp.reason`` being preset to the arguments of ``synth()``.
##
## .. _pass:
##
## ``pass``
## ~~~~~~~~
##
##     Switch to pass mode, making the current request not use the cache
##     and not putting its response into it. Control will eventually pass to
##     :ref:`vcl_pass`.
##
## .. _pipe:
##
## ``pipe``
## ~~~~~~~~
##
##     Switch to pipe mode. Control will eventually pass to
##     :ref:`vcl_pipe`.
##
## .. _restart:
##
## ``restart``
## ~~~~~~~~~~~
##
##     Restart the transaction. Increases the ``req.restarts`` counter.
##
##     If the number of restarts is higher than the *max_restarts*
##     parameter, control is passed to :ref:`vcl_synth` as for
##     ``return(synth(503, "Too many restarts"))``
##
##     For a restart, all modifications to ``req`` attributes are
##     preserved except for ``req.restarts`` and ``req.xid``, which need
##     to change by design.
##
## .. _vcl_recv:
##
## vcl_recv
## ~~~~~~~~
##
## Called at the beginning of a request, after the complete request has
## been received and parsed, after a `restart` or as the result of an ESI
## include.
##
## Its purpose is to decide whether or not to serve the request, possibly
## modify it and decide on how to process it further. A backend hint may
## be set as a default for the backend processing side.
##
## The `vcl_recv` subroutine may terminate with calling ``return()`` on one
## of the following keywords:
##
##   ``fail``
##     see :ref:`fail`
##
##   ``synth(status code, reason)``
##     see :ref:`synth`
##
##   ``restart``
##     see :ref:`restart`
##
##   ``pass``
##     see :ref:`pass`
##
##   ``pipe``
##     see :ref:`pipe`
##
##   ``hash``
##     Continue processing the object as a potential candidate for
##     caching. Passes the control over to :ref:`vcl_hash`.
##
##   ``purge``
##     Purge the object and it's variants. Control passes through
##     :ref:`vcl_hash` to :ref:`vcl_purge`.
##
##   ``vcl(label)``
##     Switch to vcl labelled *label*.
##
##     This will roll back the request as if ``std.rollback(req)`` was
##     called and continue vcl processing in :ref:`vcl_recv` of the vcl
##     labelled *label* as if it was the active vcl.
##
##     The ``vcl(label)`` return is only valid while the ``req.restarts``
##     count is zero and if used from the active vcl.
##
##     See the :ref:`ref_cli_vcl_label` command.
##
sub vcl_recv {
    if (req.http.host) {
        set req.http.host = req.http.host.lower();
    }
    if (req.method == "PRI") {
        # This will never happen in properly formed traffic (see: RFC7540)
        return (synth(405));
    }
    if (!req.http.host &&
      req.esi_level == 0 &&
      req.proto ~ "^(?i)HTTP/1.1") {
        # In HTTP/1.1, Host is required.
        return (synth(400));
    }
    if (req.method != "GET" &&
      req.method != "HEAD" &&
      req.method != "PUT" &&
      req.method != "POST" &&
      req.method != "TRACE" &&
      req.method != "OPTIONS" &&
      req.method != "DELETE" &&
      req.method != "PATCH") {
        # Non-RFC2616 or CONNECT which is weird.
        return (pipe);
    }

    if (req.method != "GET" && req.method != "HEAD") {
        # We only deal with GET and HEAD by default
        return (pass);
    }
    if (req.http.Authorization || req.http.Cookie) {
        # Not cacheable by default
        return (pass);
    }
    return (hash);
}

## .. _vcl_pipe:
##
## vcl_pipe
## ~~~~~~~~
##
## Called upon entering pipe mode. In this mode, the request is passed on
## to the backend, and any further data from both the client and backend
## is passed on unaltered until either end closes the
## connection. Basically, Varnish will degrade into a simple TCP proxy,
## shuffling bytes back and forth. For a connection in pipe mode, no
## other VCL subroutine will ever get called after `vcl_pipe`.
##
## The `vcl_pipe` subroutine may terminate with calling ``return()`` with one
## of the following keywords:
##
##   ``fail``
##     see   :ref:`fail`
##
##   ``synth(status code, reason)``
##     see  :ref:`synth`
##
##   ``pipe``
##     Proceed with pipe mode.
##
sub vcl_pipe {
    # By default Connection: close is set on all piped requests, to stop
    # connection reuse from sending future requests directly to the
    # (potentially) wrong backend. If you do want this to happen, you can undo
    # it here.
    # unset bereq.http.connection;
    return (pipe);
}

## .. _vcl_pass:
##
## vcl_pass
## ~~~~~~~~
##
## Called upon entering pass mode. In this mode, the request is passed
## on to the backend, and the backend's response is passed on to the
## client, but is not entered into the cache. Subsequent requests
## submitted over the same client connection are handled normally.
##
## The `vcl_pass` subroutine may terminate with calling ``return()`` with one
## of the following keywords:
##
##   ``fail``
##     see  :ref:`fail`
##
##   ``synth(status code, reason)``
##     see  :ref:`synth`
##
##   ``restart``
##     see  :ref:`restart`
##
##   ``fetch``
##     Proceed with pass mode - initiate a backend request.
##
sub vcl_pass {
    return (fetch);
}

## .. _vcl_hash:
##
## vcl_hash
## ~~~~~~~~
##
## Called after `vcl_recv` to create a hash value for the request. This is
## used as a key to look up the object in Varnish.
##
## The `vcl_hash` subroutine may terminate with calling ``return()`` with one
## of the following keywords:
##
##   ``fail``
##     see  :ref:`fail`
##
##   ``lookup``
##     Look up the object in cache.
##
##     Control passes to :ref:`vcl_purge` when coming from a ``purge``
##     return in `vcl_recv`.
##
##     Otherwise control passes to the next subroutine depending on the
##     result of the cache lookup:
##
##     * a hit: pass to :ref:`vcl_hit`
##
##     * a miss or a hit on a hit-for-miss object (an object with
##       ``obj.uncacheable == true``): pass to :ref:`vcl_miss`
##
##     * a hit on a hit-for-pass object (for which ``pass(DURATION)`` had been
##       previously returned from ``vcl_backend_response``): pass to
##       :ref:`vcl_pass`
##
sub vcl_hash {
    hash_data(req.url);
    if (req.http.host) {
        hash_data(req.http.host);
    } else {
        hash_data(server.ip);
    }
    return (lookup);
}

## .. _vcl_purge:
##
## vcl_purge
## ~~~~~~~~~
##
## Called after the purge has been executed and all its variants have been evicted.
##
## The `vcl_purge` subroutine may terminate with calling ``return()`` with one
## of the following keywords:
##
##   ``fail``
##     see  :ref:`fail`
##
##   ``synth(status code, reason)``
##     see  :ref:`synth`
##
##   ``restart``
##     see  :ref:`restart`
##
sub vcl_purge {
    return (synth(200, "Purged"));
}

## .. _vcl_hit:
##
## vcl_hit
## ~~~~~~~
##
## Called when a cache lookup is successful. The object being hit may be
## stale: It can have a zero or negative `ttl` with only `grace` or
## `keep` time left.
##
## The `vcl_hit` subroutine may terminate with calling ``return()``
## with one of the following keywords:
##
##   ``fail``
##     see  :ref:`fail`
##
##   ``synth(status code, reason)``
##     see  :ref:`synth`
##
##   ``restart``
##     see  :ref:`restart`
##
##   ``pass``
##     see  :ref:`pass`
##
##   ``deliver``
##     Deliver the object. If it is stale, a background fetch to refresh
##     it is triggered.
##
sub vcl_hit {
    return (deliver);
}

## .. _vcl_miss:
##
## vcl_miss
## ~~~~~~~~
##
## Called after a cache lookup if the requested document was not found in
## the cache or if :ref:`vcl_hit` returned ``fetch``.
##
## Its purpose is to decide whether or not to attempt to retrieve the
## document from the backend. A backend hint may be set as a default for
## the backend processing side.
##
## The `vcl_miss` subroutine may terminate with calling ``return()`` with one
## of the following keywords:
##
##   ``fail``
##     see  :ref:`fail`
##
##   ``synth(status code, reason)``
##     see  :ref:`synth`
##
##   ``restart``
##     see  :ref:`restart`
##
##   ``pass``
##     see  :ref:`pass`
##
##   ``fetch``
##     Retrieve the requested object from the backend. Control will
##     eventually pass to `vcl_backend_fetch`.
##
sub vcl_miss {
    return (fetch);
}

## .. _vcl_deliver:
##
## vcl_deliver
## ~~~~~~~~~~~
##
## Called before any object except a `vcl_synth` result is delivered to the client.
##
## The `vcl_deliver` subroutine may terminate with calling ``return()`` with one
## of the following keywords:
##
##   ``fail``
##     see  :ref:`fail`
##
##   ``synth(status code, reason)``
##     see  :ref:`synth`
##
##   ``restart``
##     see  :ref:`restart`
##
##   ``deliver``
##     Deliver the object to the client.
##
## .. _vcl_synth:
##
sub vcl_deliver {
    return (deliver);
}

## vcl_synth
## ~~~~~~~~~
##
## Called to deliver a synthetic object. A synthetic object is generated
## in VCL, not fetched from the backend. Its body may be constructed using
## the ``synthetic()`` function or by setting the `resp.body` variable.
##
## The  `vcl_synth` subroutine can be entered implicitely with the
## following errors: 500 and 503.
##
## A `vcl_synth` defined object never enters the cache, contrary to a
## :ref:`vcl_backend_error` defined object, which may end up in cache.
##
## The subroutine may terminate with calling ``return()`` with one of the
## following keywords:
##
##   ``fail``
##     see  :ref:`fail`
##
##   ``restart``
##     see  :ref:`restart`
##
##   ``deliver``
##     Directly deliver the object defined by `vcl_synth` to the client
##     without calling `vcl_deliver`.
##
sub vcl_synth {
    set resp.http.Content-Type = "text/html; charset=utf-8";
    set resp.http.Retry-After = "5";
    set resp.body = {"<!DOCTYPE html>
<html>
  <head>
    <title>"} + resp.status + " " + resp.reason + {"</title>
  </head>
  <body>
    <h1>Error "} + resp.status + " " + resp.reason + {"</h1>
    <p>"} + resp.reason + {"</p>
    <h3>Guru Meditation:</h3>
    <p>XID: "} + req.xid + {"</p>
    <hr>
    <p>Varnish cache server</p>
  </body>
</html>
"};
    return (deliver);
}

## Backend Side
## ------------
##
## The following subroutines are part of the backend state machine, they
## manage the lifecycle of backend requests and responses and may insert
## objects in the cache. There is one common action shared by all backend
## subroutines.
##
## .. _abandon:
##
## ``abandon``
## ~~~~~~~~~~~
##
##     Abandon the backend request. Unless the backend request was a
##     background fetch, control is passed to :ref:`vcl_synth` on the
##     client side with ``resp.status`` preset to 503.
##
## .. _vcl_backend_fetch:
##
## vcl_backend_fetch
## ~~~~~~~~~~~~~~~~~
##
## Called before sending the backend request. In this subroutine you
## typically alter the request before it gets to the backend.
##
## The `vcl_backend_fetch` subroutine may terminate with calling
## ``return()`` with one of the following keywords:
##
##   ``fail``
##     see  :ref:`fail`
##
##   ``abandon``
##     see  :ref:`abandon`
##
##   ``fetch``
##     Fetch the object from the backend.
##
##   ``error(status code, reason)``
##     Transition to :ref:`vcl_backend_error` with ``beresp.status`` and
##     ``beresp.reason`` being preset to the arguments of ``error()`` if
##     arguments are provided.
##
## Before calling `vcl_backend_fetch`, Varnish core prepares the `bereq`
## backend request as follows:
##
## * Unless the request is a `pass`,
##
##   * set ``bereq.method`` to ``GET`` and ``bereq.proto`` to
##     ``HTTP/1.1`` and
##
##   * set ``bereq.http.Accept_Encoding`` to ``gzip`` if
##     :ref:`ref_param_http_gzip_support` is enabled.
##
## * If there is an existing cache object to be revalidated, set
##   ``bereq.http.If-Modified-Since`` from its ``Last-Modified`` header
##   and/or set ``bereq.http.If-None-Match`` from its ``Etag`` header
##
## * Set ``bereq.http.X-Varnish`` to the current transaction id (`vxid`)
##
## These changes can be undone or modified in `vcl_backend_fetch` before
## the backend request is issued.
##
## In particular, to cache non-GET requests, ``req.method`` needs to be
## saved to a header or variable in :ref:`vcl_recv` and restored to
## ``bereq.method``. Notice that caching non-GET requests typically also
## requires changing the cache key in :ref:`vcl_hash` e.g. by also
## hashing the request method and/or request body.
##
## HEAD request can be satisfied from cached GET responses.
##
sub vcl_backend_fetch {
    if (bereq.method == "GET") {
        unset bereq.body;
    }
    return (fetch);
}

## .. _vcl_backend_response:
##
## vcl_backend_response
## ~~~~~~~~~~~~~~~~~~~~
##
## Called after the response headers have been successfully retrieved from
## the backend.
##
## The `vcl_backend_response` subroutine may terminate with calling
## ``return()`` with one of the following keywords:
##
##   ``fail``
##     see  :ref:`fail`
##
##   ``abandon``
##     see  :ref:`abandon`
##
##   ``deliver``
##     For a 304 response, create an updated cache object.
##     Otherwise, fetch the object body from the backend and initiate
##     delivery to any waiting client requests, possibly in parallel
##     (streaming).
##
##   ``retry``
##     Retry the backend transaction. Increases the `retries` counter.
##     If the number of retries is higher than *max_retries*,
##     control will be passed to :ref:`vcl_backend_error`.
##
##   ``pass(duration)``
##     Mark the object as a hit-for-pass for the given duration. Subsequent
##     lookups hitting this object will be turned into passed transactions,
##     as if ``vcl_recv`` had returned ``pass``.
##
##   ``error(status code, reason)``
##     Transition to :ref:`vcl_backend_error` with ``beresp.status`` and
##     ``beresp.reason`` being preset to the arguments of ``error()`` if
##     arguments are provided.
##
## 304 handling
## ~~~~~~~~~~~~
##
## For a 304 response, Varnish core code amends ``beresp`` before calling
## `vcl_backend_response`:
##
## * If the gzip status changed, ``Content-Encoding`` is unset and any
##   ``Etag`` is weakened
##
## * Any headers not present in the 304 response are copied from the
##   existing cache object. ``Content-Length`` is copied if present in
##   the existing cache object and discarded otherwise.
##
## * The status gets set to 200.
##
## `beresp.was_304` marks that this conditional response processing has
## happened.
##
## Note: Backend conditional requests are independent of client
## conditional requests, so clients may receive 304 responses no matter
## if a backend request was conditional.
##
## beresp.ttl / beresp.grace / beresp.keep
## ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
##
## Before calling `vcl_backend_response`, core code sets ``beresp.ttl``
## based on the response status and the response headers ``Age``,
## ``Cache-Control`` or ``Expires`` and ``Date`` as follows:
##
## * If present and valid, the value of the ``Age`` header is effectively
##   deduced from all ttl calculations.
##
## * For status codes 200, 203, 204, 300, 301, 304, 404, 410 and 414:
##
##   * If ``Cache-Control`` contains an ``s-maxage`` or ``max-age`` field
##     (in that order of preference), the ttl is set to the respective
##     non-negative value or 0 if negative.
##
##   * Otherwise, if no ``Expires`` header exists, the default ttl is
##     used.
##
##   * Otherwise, if ``Expires`` contains a time stamp before ``Date``,
##     the ttl is set to 0.
##
##   * Otherwise, if no ``Date`` header is present or the ``Date`` header
##     timestamp differs from the local clock by no more than the
##     `clock_skew` parameter, the ttl is set to
##
##     * 0 if ``Expires`` denotes a past timestamp or
##
##     * the difference between the local clock and the ``Expires``
##       header otherwise.
##
##   * Otherwise, the ttl is set to the difference between ``Expires``
##     and ``Date``
##
## * For status codes 302 and 307, the calculation is identical except
##   that the default ttl is not used and -1 is returned if neither
##   ``Cache-Control`` nor ``Expires`` exists.
##
## * For all other status codes, ttl -1 is returned.
##
## ``beresp.grace`` defaults to the `default_grace` parameter.
##
## For a non-negative ttl, if ``Cache-Control`` contains a
## ``stale-while-revalidate`` field value, ``beresp.grace`` is
## set to that value if non-negative or 0 otherwise.
##
## ``beresp.keep`` defaults to the `default_keep` parameter.
##
sub vcl_backend_response {
    if (bereq.uncacheable) {
        return (deliver);
    } else if (beresp.ttl <= 0s ||
      beresp.http.Set-Cookie ||
      beresp.http.Surrogate-control ~ "(?i)no-store" ||
      (!beresp.http.Surrogate-Control &&
        beresp.http.Cache-Control ~ "(?i:no-cache|no-store|private)") ||
      beresp.http.Vary == "*") {
        # Mark as "Hit-For-Miss" for the next 2 minutes
        set beresp.ttl = 120s;
        set beresp.uncacheable = true;
    }
    return (deliver);
}

## .. _vcl_backend_error:
##
## vcl_backend_error
## ~~~~~~~~~~~~~~~~~
##
## This subroutine is called if we fail the backend fetch or if
## *max_retries* has been exceeded.
##
## Returning with :ref:`abandon` does not leave a cache object.
##
## If returning with ``deliver`` and a ``beresp.ttl > 0s``, a synthetic
## cache object is generated in VCL, whose body may be constructed using
## the ``synthetic()`` function.
##
## When there is a waiting list on the object, the default ``ttl`` will
## be positive (currently one second), set before entering
## ``vcl_backend_error``. This is to avoid request serialization and
## hammering on a potentially failing backend.
##
## Since these synthetic objects are cached in these special
## circumstances, be cautious with putting private information there. If
## you really must, then you need to explicitly set ``beresp.ttl`` to
## zero in ``vcl_backend_error``.
##
## The `vcl_backend_error` subroutine may terminate with calling ``return()``
## with one of the following keywords:
##
##   ``fail``
##     see  :ref:`fail`
##
##   ``abandon``
##     see  :ref:`abandon`
##
##   ``deliver``
##     Deliver and possibly cache the object defined in
##     `vcl_backend_error` **as if it was fetched from the backend**, also
##     referred to as a "backend synth".
##
##   ``retry``
##     Retry the backend transaction. Increases the `retries` counter.
##     If the number of retries is higher than *max_retries*,
##     :ref:`vcl_synth` on the client side is called with ``resp.status``
##     preset to 503.
##
sub vcl_backend_error {
    set beresp.http.Content-Type = "text/html; charset=utf-8";
    set beresp.http.Retry-After = "5";
    set beresp.body = {"<!DOCTYPE html>
<html>
  <head>
    <title>"} + beresp.status + " " + beresp.reason + {"</title>
  </head>
  <body>
    <h1>Error "} + beresp.status + " " + beresp.reason + {"</h1>
    <p>"} + beresp.reason + {"</p>
    <h3>Guru Meditation:</h3>
    <p>XID: "} + bereq.xid + {"</p>
    <hr>
    <p>Varnish cache server</p>
  </body>
</html>
"};
    return (deliver);
}

## Housekeeping
## ------------
##
## The following subroutines run during ``vcl.load`` and ``vcl.discard``.
##
## .. _vcl_init:
##
## vcl_init
## ~~~~~~~~
##
## Called when VCL is loaded, before any requests pass through it.
## Typically used to initialize VMODs.
##
## The `vcl_init` subroutine may terminate with calling ``return()``
## with one of the following keywords:
##
##   ``ok``
##     Normal return, VCL continues loading.
##
##   ``fail``
##     Abort loading of this VCL.
##
## .. _vcl_fini:
##
sub vcl_init {
    return (ok);
}

## vcl_fini
## ~~~~~~~~
##
## Called when VCL is discarded only after all requests have exited the VCL.
## Typically used to clean up VMODs.
##
## The `vcl_fini` subroutine may terminate with calling ``return()``
## with one of the following keywords:
##
##   ``ok``
##     Normal return, VCL will be discarded.
##
sub vcl_fini {
    return (ok);
}
