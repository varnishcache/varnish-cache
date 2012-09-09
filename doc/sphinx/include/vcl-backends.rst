Backend declarations
--------------------

A backend declaration creates and initializes a named backend object:
::

  backend www {
    .host = "www.example.com";
    .port = "http";
  }

The backend object can later be used to select a backend at request time:
::

  if (req.http.host ~ "(?i)^(www.)?example.com$") {
    set req.backend = www;
  }

To avoid overloading backend servers, .max_connections can be set to
limit the maximum number of concurrent backend connections.

The timeout parameters can be overridden in the backend declaration.
The timeout parameters are .connect_timeout for the time to wait for a
backend connection, .first_byte_timeout for the time to wait for the
first byte from the backend and .between_bytes_timeout for time to
wait between each received byte.

These can be set in the declaration like this:
::

  backend www {
    .host = "www.example.com";
    .port = "http";
    .connect_timeout = 1s;
    .first_byte_timeout = 5s;
    .between_bytes_timeout = 2s;
  }

To mark a backend as unhealthy after number of items have been added
to its saintmode list ``.saintmode_threshold`` can be set to the maximum
list size. Setting a value of 0 disables saint mode checking entirely
for that backend.  The value in the backend declaration overrides the
parameter.

