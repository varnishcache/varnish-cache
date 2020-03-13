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
  more stable backend selection through consistent hashing. See
  :ref:`vmod_directors(3)` for details.

* We fixed a case where ``send_timeout`` had no effect on HTTP/1
  connections when streaming from a backend fetch, in other words, a
  connection might not have got closed despite the ``send_timeout``
  having been reached. HTTP/2 was not affected.

  When ``send_timeout`` is reached on HTTP/1, the ``MAIN.sc_tx_error``
  is increased and a ``Debug`` message ``Hit total send timeout, wrote
  = x/y; not retrying`` is logged.

  .. actually H2 is really quite different and we have a plan to
     harmonize timeout handling across the board

  Users with long running backend fetches and HTTP/1 clients should
  watch out for an increase of the ``MAIN.sc_tx_error`` compared to
  before the deployment and consider increasing ``send_timeout``
  appropriately.

  The timeout can also be set per connection from VCL as
  ``sess.send_timeout``.

Statistics
----------

* The ``MAIN.sess_drop`` counter is gone. It should be removed from
  any statistics gathering tools, if present

* ``timeout_idle`` being reached on HTTP/1 used to be accounted to the
  ``MAIN.rx_timeout`` statistic. We have now added the
  ``MAIN.rx_close_idle`` counter for this case specifically.

* ``send_timeout`` being reached on HTTP/1 used to be accounted to
  ``MAIN.sc_rem_close``. Such timeout events are now accounted towards
  ``MAIN.sc_tx_error``.

vsl/logs
--------

* The ``Process`` timestamp for ``vcl_synth {}`` was wrongly issued
  before the VCL subroutine was called, now it gets emitted after VCL
  returns for consistency with ``vcl_deliver {}``.

  Users of this timestamp should be aware that it now includes
  ``vcl_synth {}`` processing time and appears at a different
  position in the log.

* A ``Notice`` VSL tag has been added

*eof*
