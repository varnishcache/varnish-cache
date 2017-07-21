
Adding WebSockets support
-------------------------

WebSockets is a technology for creating a bidirectional stream-based
channel over HTTP.

To run WebSockets through Varnish you need to pipe the request and copy
the Upgrade and Connection headers as follows::

    sub vcl_recv {
        if (req.http.upgrade ~ "(?i)websocket") {
            return (pipe);
        }
    }

    sub vcl_pipe {
        if (req.http.upgrade) {
            set bereq.http.upgrade = req.http.upgrade;
            set bereq.http.connection = req.http.connection;
        }
    }
