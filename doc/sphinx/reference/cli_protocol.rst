.. role:: ref(emphasis)

.. _ref_cli_api:

===========================================
VCLI protocol - Scripting the CLI interface
===========================================

The Varnish CLI has a few bells&whistles when used as an API.

First: `vcli.h` contains magic numbers.

Second: If you use `varnishadm` to connect to `varnishd` use the
`-p` argument to get "pass" mode.

In "pass" mode, or with direct CLI connections (more below), the
first line of responses is always exactly 13 bytes long, including
the NL, and it contains two numbers:  The status code and the count
of bytes in the remainder of the response::

    200␣19␤
    PONG␣1613397488␣1.0

This makes parsing the response unambiguous, even in cases like this
where the response does not end with a NL.

The varnishapi library contains functions to implement the basics of
the CLI protocol, for more, see the `vcli.h` include file.

.. _ref_psk_auth:

Authentication CLI connections
------------------------------

CLI connections to `varnishd` are authenticated with a "pre-shared-key"
authentication scheme, where the other end must prove they know the
contents of a particular file, either by being able to access it on
the machine `varnishd` runs on, usually via information in `VSM` or
by having a local copy of the file on another machine.

The precise filename can be configured with the `-S` option to `varnishd`
and regular file system permissions control access to it.

The file is only read at the time the `auth` CLI command is issued
and the contents is not cached in `varnishd`, so it is possible to
change the contents of the file while `varnishd` is running.

An authenticated session looks like this:

.. code-block:: text

   critter phk> telnet localhost 1234
   Trying ::1...
   Trying 127.0.0.1...
   Connected to localhost.
   Escape character is '^]'.
   107 59
   ixslvvxrgkjptxmcgnnsdxsvdmvfympg

   Authentication required.

   auth 455ce847f0073c7ab3b1465f74507b75d3dc064c1e7de3b71e00de9092fdc89a
   200 279
   -----------------------------
   Varnish Cache CLI 1.0
   -----------------------------
   FreeBSD,13.0-CURRENT,amd64,-jnone,-sdefault,-sdefault,-hcritbit
   varnish-trunk revision 89a558e56390d425c52732a6c94087eec9083115

   Type 'help' for command list.
   Type 'quit' to close CLI session.
   Type 'start' to launch worker process.

The CLI status of 107 indicates that authentication is necessary. The
first 32 characters of the response text is the challenge
"ixsl...mpg". The challenge is randomly generated for each CLI
connection, and changes each time a 107 is emitted.

The most recently emitted challenge must be used for calculating the
authenticator "455c...c89a".

The authenticator is calculated by applying the SHA256 function to the
following byte sequence:

* Challenge string
* Newline (0x0a) character.
* Contents of the secret file
* Challenge string
* Newline (0x0a) character.

and dumping the resulting digest in lower-case hex.

In the above example, the secret file contains ``foo\n`` and thus:

.. code-block:: text

   critter phk> hexdump secret
   00000000  66 6f 6f 0a                                       |foo.|
   00000004
   critter phk> cat > tmpfile
   ixslvvxrgkjptxmcgnnsdxsvdmvfympg
   foo
   ixslvvxrgkjptxmcgnnsdxsvdmvfympg
   ^D
   critter phk> hexdump -C tmpfile
   00000000  69 78 73 6c 76 76 78 72  67 6b 6a 70 74 78 6d 63  |ixslvvxrgkjptxmc|
   00000010  67 6e 6e 73 64 78 73 76  64 6d 76 66 79 6d 70 67  |gnnsdxsvdmvfympg|
   00000020  0a 66 6f 6f 0a 69 78 73  6c 76 76 78 72 67 6b 6a  |.foo.ixslvvxrgkj|
   00000030  70 74 78 6d 63 67 6e 6e  73 64 78 73 76 64 6d 76  |ptxmcgnnsdxsvdmv|
   00000040  66 79 6d 70 67 0a                                 |fympg.|
   00000046
   critter phk> sha256 tmpfile
   SHA256 (_) = 455ce847f0073c7ab3b1465f74507b75d3dc064c1e7de3b71e00de9092fdc89a
   critter phk> openssl dgst -sha256 < tmpfile
   455ce847f0073c7ab3b1465f74507b75d3dc064c1e7de3b71e00de9092fdc89a

The sourcefile `lib/libvarnish/cli_auth.c` contains a useful function
which calculates the response, given an open filedescriptor to the
secret file, and the challenge string.

See also:
---------

* :ref:`varnishadm(1)`
* :ref:`varnishd(1)`
* :ref:`vcl(7)`
