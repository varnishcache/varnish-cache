.. _users-guide-statistics:


Statistics
----------

Now that your Varnish is up and running let's have a look at how it is
doing. There are several tools that can help.

varnishtop
~~~~~~~~~~

The varnishtop utility reads the shared memory logs and presents a
continuously updated list of the most commonly occurring log entries.

With suitable filtering using the -I, -i, -X and -x options, it can be
used to display a ranking of requested documents, clients, user
agents, or any other information which is recorded in the log.

``varnishtop -i rxurl`` will show you what URLs are being asked for
by the client. ``varnishtop -i txurl`` will show you what your backend
is being asked the most. ``varnishtop -i RxHeader -I
Accept-Encoding`` will show the most popular Accept-Encoding header
the client are sending you.

For more information please see :ref:`ref-varnishtop`.

varnishhist
~~~~~~~~~~~

The varnishhist utility reads varnishd(1) shared memory logs and
presents a continuously updated histogram showing the distribution of
the last N requests by their processing.  The value of N and the
vertical scale are displayed in the top left corner.  The horizontal
scale is logarithmic.  Hits are marked with a pipe character ("|"),
and misses are marked with a hash character ("#").

For more information please see :ref:`ref-varnishhist`.


varnishstat
~~~~~~~~~~~

Varnish has lots of counters. We count misses, hits, information about
the storage, threads created, deleted objects. Just about
everything. varnishstat will dump these counters. This is useful when
tuning Varnish.

There are programs that can poll varnishstat regularly and make nice
graphs of these counters. One such program is Munin. Munin can be
found at http://munin-monitoring.org/ . There is a plugin for munin in
the Varnish source code.

For more information please see :ref:`ref-varnishstat`.
