.. _users-guide-devicedetect:

Device detection
~~~~~~~~~~~~~~~~

Device detection is figuring out what kind of content to serve to a
client based on the User-Agent string supplied in a request.

Use cases for this are for example to send size reduced files to mobile
clients with small screens and on high latency networks, or to 
provide a streaming video codec that the client understands.

There are a couple of typical strategies to use for this type of scenario:
1) Redirect to another URL.
2) Use a different backend for the special client.
3) Change the backend request so that the backend sends tailored content.

To perhaps make the strategies easier to understand, we, in this context, assume
that the `req.http.X-UA-Device` header is present and unique per client class. 

Setting this header can be as simple as::

   sub vcl_recv {
       if (req.http.User-Agent ~ "(?i)iphone" {
           set req.http.X-UA-Device = "mobile-iphone";
       }
   }

There are different commercial and free offerings in doing grouping and
identifying clients in further detail. For a basic and community
based regular expression set, see
https://github.com/varnish/varnish-devicedetect/.


Serve the different content on the same URL
-------------------------------------------

The tricks involved are: 
1. Detect the client (pretty simple, just include `devicedetect.vcl` and call
it).
2. Figure out how to signal the backend the client class. This
includes for example setting a header, changing a header or even changing the
backend request URL.
3. Modify any response from the backend to add missing 'Vary' headers, so
Varnish' internal handling of this kicks in.
4. Modify output sent to the client so any caches outside our control don't
serve the wrong content.

All this needs to be done while still making sure that we only get one cached object per URL per
device class.


Example 1: Send HTTP header to backend
''''''''''''''''''''''''''''''''''''''

The basic case is that Varnish adds the 'X-UA-Device' HTTP header on the backend
requests, and the backend mentions in the response 'Vary' header that the content
is dependant on this header. 

Everything works out of the box from Varnish' perspective.

.. 071-example1-start

VCL::

    sub vcl_recv { 
        # call some detection engine that set req.http.X-UA-Device
    }
    # req.http.X-UA-Device is copied by Varnish into bereq.http.X-UA-Device

    # so, this is a bit counterintuitive. The backend creates content based on
    # the normalized User-Agent, but we use Vary on X-UA-Device so Varnish will
    # use the same cached object for all U-As that map to the same X-UA-Device.
    #
    # If the backend does not mention in Vary that it has crafted special
    # content based on the User-Agent (==X-UA-Device), add it. 
    # If your backend does set Vary: User-Agent, you may have to remove that here.
    sub vcl_backend_response {
        if (bereq.http.X-UA-Device) {
            if (!beresp.http.Vary) { # no Vary at all
                set beresp.http.Vary = "X-UA-Device"; 
            } elseif (beresp.http.Vary !~ "X-UA-Device") { # add to existing Vary
                set beresp.http.Vary = beresp.http.Vary + ", X-UA-Device"; 
            } 
        }
        # comment this out if you don't want the client to know your
        # classification
        set beresp.http.X-UA-Device = bereq.http.X-UA-Device;
    }

    # to keep any caches in the wild from serving wrong content to client #2
    # behind them, we need to transform the Vary on the way out.
    sub vcl_deliver {
        if ((req.http.X-UA-Device) && (resp.http.Vary)) {
            set resp.http.Vary = regsub(resp.http.Vary, "X-UA-Device", "User-Agent");
        }
    }

.. 071-example1-end

Example 2: Normalize the User-Agent string
''''''''''''''''''''''''''''''''''''''''''

Another way of signalling the device type is to override or normalize the
'User-Agent' header sent to the backend.

For example::

    User-Agent: Mozilla/5.0 (Linux; U; Android 2.2; nb-no; HTC Desire Build/FRF91) AppleWebKit/533.1 (KHTML, like Gecko) Version/4.0 Mobile Safari/533.1

becomes::

    User-Agent: mobile-android

when seen by the backend.

This works if you don't need the original header for anything on the backend.
A possible use for this is for CGI scripts where only a small set of predefined
headers are (by default) available for the script.

.. 072-example2-start

VCL::

    sub vcl_recv { 
        # call some detection engine that set req.http.X-UA-Device
    }

    # override the header before it is sent to the backend
    sub vcl_miss { if (req.http.X-UA-Device) { set bereq.http.User-Agent = req.http.X-UA-Device; } }
    sub vcl_pass { if (req.http.X-UA-Device) { set bereq.http.User-Agent = req.http.X-UA-Device; } }

    # standard Vary handling code from previous examples.
    sub vcl_backend_response {
        if (bereq.http.X-UA-Device) {
            if (!beresp.http.Vary) { # no Vary at all
                set beresp.http.Vary = "X-UA-Device";
            } elseif (beresp.http.Vary !~ "X-UA-Device") { # add to existing Vary
                set beresp.http.Vary = beresp.http.Vary + ", X-UA-Device";
            }
        }
        set beresp.http.X-UA-Device = bereq.http.X-UA-Device;
    }
    sub vcl_deliver {
        if ((req.http.X-UA-Device) && (resp.http.Vary)) {
            set resp.http.Vary = regsub(resp.http.Vary, "X-UA-Device", "User-Agent");
        }
    }

.. 072-example2-end

Example 3: Add the device class as a GET query parameter
''''''''''''''''''''''''''''''''''''''''''''''''''''''''

If everything else fails, you can add the device type as a GET argument.

    http://example.com/article/1234.html --> http://example.com/article/1234.html?devicetype=mobile-iphone

The client itself does not see this classification, only the backend request
is changed.

.. 073-example3-start

VCL::

    sub vcl_recv { 
        # call some detection engine that set req.http.X-UA-Device
    }

    sub append_ua {
        if ((req.http.X-UA-Device) && (req.method == "GET")) {
            # if there are existing GET arguments;
            if (req.url ~ "\?") {
                set req.http.X-get-devicetype = "&devicetype=" + req.http.X-UA-Device;
            } else { 
                set req.http.X-get-devicetype = "?devicetype=" + req.http.X-UA-Device;
            }
            set req.url = req.url + req.http.X-get-devicetype;
            unset req.http.X-get-devicetype;
        }
    }

    # do this after vcl_hash, so all Vary-ants can be purged in one go. (avoid ban()ing)
    sub vcl_miss { call append_ua; }
    sub vcl_pass { call append_ua; }

    # Handle redirects, otherwise standard Vary handling code from previous
    # examples.
    sub vcl_backend_response {
        if (bereq.http.X-UA-Device) {
            if (!beresp.http.Vary) { # no Vary at all
                set beresp.http.Vary = "X-UA-Device";
            } elseif (beresp.http.Vary !~ "X-UA-Device") { # add to existing Vary
                set beresp.http.Vary = beresp.http.Vary + ", X-UA-Device";
            }

            # if the backend returns a redirect (think missing trailing slash),
            # we will potentially show the extra address to the client. we
            # don't want that.  if the backend reorders the get parameters, you
            # may need to be smarter here. (? and & ordering)

            if (beresp.status == 301 || beresp.status == 302 || beresp.status == 303) {
                set beresp.http.location = regsub(beresp.http.location, "[?&]devicetype=.*$", "");
            }
        }
        set beresp.http.X-UA-Device = bereq.http.X-UA-Device;
    }
    sub vcl_deliver {
        if ((req.http.X-UA-Device) && (resp.http.Vary)) {
            set resp.http.Vary = regsub(resp.http.Vary, "X-UA-Device", "User-Agent");
        }
    }

.. 073-example3-end

Different backend for mobile clients
------------------------------------

If you have a different backend that serves pages for mobile clients, or any
special needs in VCL, you can use the 'X-UA-Device' header like this::

    backend mobile {
        .host = "10.0.0.1";
        .port = "80";
    }

    sub vcl_recv {
        # call some detection engine

        if (req.http.X-UA-Device ~ "^mobile" || req.http.X-UA-device ~ "^tablet") {
            set req.backend_hint = mobile;
        }
    }
    sub vcl_hash {
        if (req.http.X-UA-Device) {
            hash_data(req.http.X-UA-Device);
        }
    }

Redirecting mobile clients
--------------------------

If you want to redirect mobile clients you can use the following snippet.

.. 065-redir-mobile-start

VCL::

    sub vcl_recv {
        # call some detection engine

        if (req.http.X-UA-Device ~ "^mobile" || req.http.X-UA-device ~ "^tablet") {
            return(synth(750, "Moved Temporarily"));
        }
    }
     
    sub vcl_synth {
        if (obj.status == 750) {
            set obj.http.Location = "http://m.example.com" + req.url;
            set obj.status = 302;
            return(deliver);
        }
    }

.. 065-redir-mobile-end


