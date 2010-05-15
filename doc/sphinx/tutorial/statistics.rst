.. _tutorial-statistics:


Statistics
----------

Now that your varnish is up and running lets have a look at how it is
doing. There are several tools that can help.

varnishtop
==========

The varnishtop utility reads the shared memory logs and presents a
continuously updated list of the most commonly occurring log entries.

With suitable filtering using the -I, -i, -X and -x options, it can be
used to display a ranking of requested documents, clients, user
agents, or any other information which is recorded in the log.

XXX Show some nice examples here.

varnishhist
===========

The varnishhist utility reads varnishd(1) shared memory logs and
presents a continuously updated histogram showing the distribution of
the last N requests by their processing.  The value of N and the
vertical scale are displayed in the top left corner.  The horizontal
scale is logarithmic.  Hits are marked with a pipe character ("|"),
and misses are marked with a hash character ("#").


varnishsizes
============

Varnishsizes does the same as varnishhist, except it shows the size of
the objects and not the time take to complete the request. This gives
you a good overview of how big the objects you are serving are.


varnishstat
===========

Varnish has lots of counters. We count misses, hits, information about
the storage, threads created, deleted objects. Just about
everything. varnishstat will dump these counters. This is useful when
tuning varnish. 

There are programs that can poll varnishstat regularly and make nice
graphs of these counters. One such program is Munin. Munin can be
found at http://munin-monitoring.org/ . There is a plugin for munin in
the varnish source code.


