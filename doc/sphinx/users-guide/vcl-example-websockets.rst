
Adding WebSockets support
-------------------------

WebSockets is a technology for creating a bidirectional stream-based
channel over HTTP.

To run WebSockets through Varnish you need to pipe it as follows::

    sub vcl_recv {
        if (req.http.upgrade ~ "(?i)websocket") {
            return (pipe);
        }
    }
