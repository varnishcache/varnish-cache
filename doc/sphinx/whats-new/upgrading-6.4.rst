.. _whatsnew_upgrading_6.4:

%%%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish 6.4.0
%%%%%%%%%%%%%%%%%%%%%%%%%%

Upgrading to Varnish 6.4 from 6.3 should not require any changes
to VCL.

This document contains information about other relevant aspects which
should be considered when upgrading.

varnishd
--------

* The hash algorithm of the ``hash`` director was changed, so backend
  selection will change once only when upgrading.

  Users of the ``hash`` director are advised to consider using the
  ``shard`` director instead, which, amongst other advantages, offers
  more stable backend selection through consistent hashing.

* We fixed a case where ``send_timeout`` had no effect when streaming
  from a backend fetch.

  Users with long running backend fetches should watch out of
  increases connection close rates and consider increasing
  ``send_timeout`` appropriately.

  The timeout can also be set per connection from vcl as
  ``sess.send_timeout``.

Statistics
----------

* The ``MAIN.sess_drop`` counter is gone. It should be removed from
  any statistics gathering tools, if present

* Added ``rx_close_idle`` counter for separate accounting when
  ``timeout_idle`` is reached. Also, ``send_timeout`` is no longer
  reported as "remote closed".

vsl/logs
--------

* The ``Process`` timestamp for ``vcl_synth {}`` was wrongly issued
  before the VCL subroutine, now it gets emitted after VCL returns for
  consistency with ``vcl_deliver {}``.

  Users of this timestamp should be aware that it now includes
  ``vcl_synth {}`` processing time and appears at a different
  position.

* A ``Notice`` VSL tag has been added

*eof*
