.. _users-guide-logging:

Logging in Varnish
------------------

One of the really nice features in Varnish is the way logging
works. Instead of logging to a normal log file Varnish logs to a shared
memory segment, called the VSL - the Varnish Shared Log. When the end
of the segment is reached we start over, overwriting old data. 

This is much, much faster than logging to a file and it doesn't
require disk space. Besides it gives you much, much more information
when you need it.

The flip side is that if you forget to have a program actually write the
logs to disk they will be overwritten.

`varnishlog` is one of the programs you can use to look at what Varnish
is logging. `varnishlog` gives you the raw logs, everything that is
written to the logs. There are other clients that can access the logs as well, we'll show you
these later.

In the terminal window you started Varnish now type ``varnishlog -g raw``
and press enter.

You'll see lines like these scrolling slowly by.::

    0 CLI            - Rd ping
    0 CLI            - Wr 200 19 PONG 1273698726 1.0

These is the Varnish master process checking up on the caching process
to see that everything is OK.

Now go to the browser and reload the page displaying your web
app. 
.. XXX:Doesn't this require a setup of a running varnishd and a web application being cached? benc

You'll see lines like these.::

   11 SessOpen       c 127.0.0.1 58912 :8080 0.0.0.0 8080 1273698726.933590 14
   11 ReqStart       c 127.0.0.1 58912
   11 ReqMethod      c GET
   11 ReqURL         c /
   11 ReqProtocol    c HTTP/1.1
   11 ReqHeader      c Host: localhost:8080
   11 ReqHeader      c Connection: keep-alive


The first column is an arbitrary number, it identifies the
transaction. Lines with the same number are coming from the same
transaction. The second column is the *tag* of the log message. All
log entries are tagged with a tag indicating what sort of activity is
being logged.

The third column tell us whether this is is data coming from or going
to the client ('c'), or the backend ('b'). The forth column is the data
being logged.

Now, you can filter quite a bit with `varnishlog`. The basic options we think you
want to know are:

'-b'
 Only show log lines from traffic going between Varnish and the backend
 servers. This will be useful when we want to optimize cache hit rates.

'-c'
 Same as '-b' but for client side traffic.

'-g request'
 Group transactions by request.

'-q query'
 Only list transactions matching this query.

.. XXX:Maybe a couple of sample commands here? benc

For more information on this topic please see :ref:`ref-varnishlog`.
