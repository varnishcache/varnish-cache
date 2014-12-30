.. _users-guide-statistics:


Statistics
----------

Varnish comes with a couple of nifty and very useful statistics generating tools that generates statistics in real time by constantly updating and presenting a specific dataset by aggregating and analyzing logdata from the shared memory logs.

.. XXX:Heavy rewrite above. benc

varnishtop
~~~~~~~~~~

The `varnishtop` utility reads the shared memory logs and presents a
continuously updated list of the most commonly occurring log entries.

With suitable filtering using the -I, -i, -X and -x options, it can be
used to display a ranking of requested documents, clients, user
agents, or any other information which is recorded in the log.

``varnishtop -i ReqURL`` will show you what URLs are being asked for by
the client. ``varnishtop -i BereqURL`` will show you what your backend
is being asked the most. ``varnishtop -I ReqHeader:Accept-Encoding`` will
show the most popular Accept-Encoding header the client are sending you.

For more information please see :ref:`ref-varnishtop`.

varnishhist
~~~~~~~~~~~

The `varnishhist` utility reads `varnishd(1)` shared memory logs and
presents a continuously updated histogram showing the distribution of
the last N requests by their processing.  
.. XXX:1? benc
The value of N and the
vertical scale are displayed in the top left corner.  The horizontal
scale is logarithmic.  Hits are marked with a pipe character ("|"),
and misses are marked with a hash character ("#").

For more information please see :ref:`ref-varnishhist`.


varnishstat
~~~~~~~~~~~

Varnish has lots of counters. We count misses, hits, information about
the storage, threads created, deleted objects. Just about
everything. `varnishstat` will dump these counters. This is useful when
tuning Varnish.

There are programs that can poll `varnishstat` regularly and make nice
graphs of these counters. One such program is Munin. Munin can be
found at http://munin-monitoring.org/ . There is a plugin for munin in
the Varnish source code.

For more information please see :ref:`ref-varnishstat`.
