
Implementing websocket support
------------------------------

Websockets is a technology for creating a bidirectional stream-based channel over HTTP.

To run websockets through Varnish you need to pipe it, and copy the Upgrade header. Use the following
VCL config to do so::

    sub vcl_pipe {
         if (req.http.upgrade) {
             set bereq.http.upgrade = req.http.upgrade;
         }
    }
    sub vcl_recv {
         if (req.http.Upgrade ~ "(?i)websocket") {
             return (pipe);
         }
    }

.. XXX: Pipe it? maybe a bit more explanation here? benc
