$Module cookie 3 "Varnish Cookie Module"
DESCRIPTION
===========

Handle HTTP cookies easier in Varnish VCL.

Parses a cookie header into an internal data store, where per-cookie
get/set/delete functions are available. A filter_except() method removes all
but a set comma-separated list of cookies. A filter() method removes a comma-
separated list of cookies.

Regular expressions can be used for either selecting cookies, deleting matching
cookies and deleting non-matching cookie names.

A convenience function for formatting the Set-Cookie Expires date field
is also included. If there are multiple Set-Cookie headers vmod-header
should be used.

The state loaded with cookie.parse() has a lifetime of the current request
or backend request context. To pass variables to the backend request, store
the contents as fake bereq headers.

.. vcl-start

Filtering example::

    vcl 4.0;

    import cookie;

    backend default { .host = "192.0.2.11"; .port = "8080"; }

    sub vcl_recv {
        if (req.http.cookie) {
            cookie.parse(req.http.cookie);
            # Either delete the ones you want to get rid of:
            cookie.delete("cookie2");
            # or delete all but a few:
            cookie.keep("SESSIONID,PHPSESSID");

            # Store it back into req so it will be passed to the backend.
            set req.http.cookie = cookie.get_string();

            # If empty, unset so the builtin VCL can consider it for caching.
            if (req.http.cookie == "") {
                unset req.http.cookie;
            }
        }
    }

.. vcl-end

$ABI vrt
$Function VOID clean(PRIV_TASK)

Description
        Clean up previously parsed cookies. It is not necessary to run clean()
        in normal operations.
Example
        ::

                sub vcl_recv {
                        cookie.clean();
                }

$Function VOID delete(PRIV_TASK, STRING cookiename)

Description
        Delete `cookiename` from internal vmod storage if it exists.

Example
        ::

		sub vcl_recv {
		    cookie.parse("cookie1: value1; cookie2: value2;");
		    cookie.delete("cookie2");
		    // get_string() will now yield "cookie1: value1";
		}

$Function VOID filter(PRIV_TASK, STRING filterstring)

Description
        Delete all cookies from internal vmod storage that are in the
        comma-separated argument cookienames.

Example
        ::

                sub vcl_recv {
                        cookie.parse("cookie1: value1; cookie2: value2; cookie3: value3");
                        cookie.filter("cookie1,cookie2");
                        // get_string() will now yield
                        // "cookie3: value3";
                }


$Function VOID filter_re(PRIV_TASK, PRIV_CALL, STRING expression)

Description
        Delete all cookies from internal vmod storage that matches the
	regular expression `expression`.

Example
        ::

                sub vcl_recv {
                        cookie.parse("cookie1: value1; cookie2: value2; cookie3: value3");
                        cookie.filter_re("^cookie[12]$");
                        // get_string() will now yield
                        // "cookie3: value3";
                }

$Function VOID filter_except(PRIV_TASK, STRING filterstring)

Description
        DEPRECATED.  This function has been renamed to keep().


$Function VOID keep(PRIV_TASK, STRING filterstring)

Description
        Delete all cookies from internal vmod storage that is not in the
        comma-separated argument cookienames.
Example
        ::

                sub vcl_recv {
                        cookie.parse("cookie1: value1; cookie2: value2; cookie3: value3");
                        cookie.keep("cookie1,cookie2");
                        // get_string() will now yield
                        // "cookie1: value1; cookie2: value2;";
                }

$Function VOID keep_re(PRIV_TASK, PRIV_CALL, STRING expression)

Description
        Delete all cookies from internal vmod storage that does not match expression `expression`.
Example
        ::

                sub vcl_recv {
                        cookie.parse("cookie1: value1; cookie2: value2; cookie3: value3");
                        cookie.keep_re("^cookie1,cookie2");
                        // get_string() will now yield
                        // "cookie1: value1; cookie2: value2;";
                }




$Function STRING format_rfc1123(TIME now, DURATION timedelta)

Description
        Get a RFC1123 formatted date string suitable for inclusion in a
        Set-Cookie response header.

        Care should be taken if the response has multiple Set-Cookie headers.
        In that case the header vmod should be used.
Example
        ::

                sub vcl_deliver {
                        # Set a userid cookie on the client that lives for 5 minutes.
                        set resp.http.Set-Cookie = "userid=" + req.http.userid + "; Expires=" + cookie.format_rfc1123(now, 5m) + "; httpOnly";
                }

$Function STRING get(PRIV_TASK, STRING cookiename)

Description
        Get the value of `cookiename`, as stored in internal vmod storage. If `cookiename` does not exist an empty string is returned.
Example
        ::

                import std;
                sub vcl_recv {
                        cookie.parse("cookie1: value1; cookie2: value2;");
                        std.log("cookie1 value is: " + cookie.get("cookie1"));
                }

$Function STRING get_re(PRIV_TASK, PRIV_CALL, STRING expression)

Description
        Get the value of the first cookie in internal vmod storage that matches regular expression `expression`. If nothing matches, an empty string is returned.
Example
        ::

                import std;
                sub vcl_recv {
                        cookie.parse("cookie1: value1; cookie2: value2;");
                        std.log("cookie1 value is: " + cookie.get_re("^cookie1$"));
                }

$Function STRING get_string(PRIV_TASK)

Description
        Get a Cookie string value with all cookies in internal vmod storage. Does
	not modify internal storage.
Example
        ::

                sub vcl_recv {
                        cookie.parse(req.http.cookie);
                        cookie.filter_except("SESSIONID,PHPSESSID");
                        set req.http.cookie = cookie.get_string();
                }

$Function BOOL isset(PRIV_TASK, STRING cookiename)

Description
        Check if `cookiename` is set in the internal vmod storage.

Example
        ::

                import std;
                sub vcl_recv {
                        cookie.parse("cookie1: value1; cookie2: value2;");
                        if (cookie.isset("cookie2")) {
                                std.log("cookie2 is set.");
                        }
                }

$Function VOID parse(PRIV_TASK, STRING cookieheader)

Description
        Parse the cookie string in `cookieheader`. If state already exists, clean() will be run first.
Example
        ::

                sub vcl_recv {
                        cookie.parse(req.http.Cookie);
                }



$Function VOID set(PRIV_TASK, STRING cookiename, STRING value)

Description
        Set the internal vmod storage for `cookiename` to `value`.

Example
        ::

                sub vcl_recv {
                        cookie.set("cookie1", "value1");
                        std.log("cookie1 value is: " + cookie.get("cookie1"));
                }

