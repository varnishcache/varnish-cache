===========
varnishtest
===========

------------------------
Test program for Varnish
------------------------

:Author: Stig Sandbeck Mathisen
:Date:   2010-05-31
:Version: 1.0
:Manual section: 1


SYNOPSIS
========
     varnishtest [-n iter] [-q] [-v] file [file ...]

DESCRIPTION
===========

The varnishtest program is a script driven program used to test the
varnish HTTP accelerator.

The varnishtest program, when started and given one or more script
files, can create a number of threads repre senting backends, some
threads representing clients, and a varnishd process.

The following options are available:

-n iter     Run iter number of iterations.

-q          Be quiet.

-v          Be verbose.

-L port     Listen on *port*. 

-t          Dunno.

file        File to use as a script


SCRIPTS
=======

Example script
~~~~~~~~~~~~~~
::

    # Start a varnish instance called "v1"
    varnish v1 -arg "-b localhost:9080" -start
    
    # Create a server thread called "s1"
    server s1 {
        # Receive a request
        rxreq
        # Send a standard response
        txresp -hdr "Connection: close" -body "012345\n"
    }
    
    # Start the server thread
    server s1 -start
    
    # Create a client thread called "c1"
    client c1 {
        # Send a request
        txreq -url "/"
        # Wait for a response
        rxresp
    # Insist that it be a success
    expect resp.status == 200
    }
    
    # Run the client
    client c1 -run
    
    # Wait for the server to die
    server s1 -wait

    # (Forcefully) Stop the varnish instance.
    varnish v1 -stop

Example script output
~~~~~~~~~~~~~~~~~~~~~

The output, running this script looks as follows. The "bargraph" at
the beginning of the line is an indication of the level of detail in
the line. The second field where the message comes from. The rest of
the line is anyones guess :-)
::

    #  TEST tests/b00000.vtc starting
    ### v1  CMD: cd ../varnishd && ./varnishd -d -d -n v1 -a :9081 -T :9001 -b localhost:9080
    ### v1  opening CLI connection
    #### v1  debug| NB: Storage size limited to 2GB on 32 bit architecture,\n
    #### v1  debug| NB: otherwise we could run out of address space.\n
    #### v1  debug| storage_file: filename: ./varnish.Shkoq5 (unlinked) size 2047 MB.\n
    ### v1  CLI connection fd = 3
    #### v1  CLI TX| start
    #### v1  debug| Using old SHMFILE\n
    #### v1  debug| Notice: locking SHMFILE in core failed: Operation not permitted\n
    #### v1  debug| bind(): Address already in use\n
    #### v1  debug| rolling(1)...
    #### v1  debug| \n
    #### v1  debug| rolling(2)...\n
    #### v1  debug| Debugging mode, enter "start" to start child\n
    ### v1  CLI 200 <start>
    ##  s1  Starting server
    ### s1  listen on :9080 (fd 6)
    ##  c1  Starting client
    ##  c1  Waiting for client
    ##  s1  started on :9080
    ##  c1  started
    ### c1  connect to :9081
    ### c1  connected to :9081 fd is 8
    #### c1  | GET / HTTP/1.1\r\n
    #### c1  | \r\n
    ### c1  rxresp
    #### s1  Accepted socket 7
    ### s1  rxreq
    #### s1  | GET / HTTP/1.1\r\n
    #### s1  | X-Varnish: 422080121\r\n
    #### s1  | X-Forwarded-For: 127.0.0.1\r\n
    #### s1  | Host: localhost\r\n
    #### s1  | \r\n
    #### s1  http[ 0] | GET
    #### s1  http[ 1] | /
    #### s1  http[ 2] | HTTP/1.1
    #### s1  http[ 3] | X-Varnish: 422080121
    #### s1  http[ 4] | X-Forwarded-For: 127.0.0.1
    #### s1  http[ 5] | Host: localhost
    #### s1  | HTTP/1.1 200 Ok\r\n
    #### s1  | Connection: close\r\n
    #### s1  | \r\n
    #### s1  | 012345\n
    #### s1  | \r\n
    ##  s1  ending
    #### c1  | HTTP/1.1 200 Ok\r\n
    #### c1  | Content-Length: 9\r\n
    #### c1  | Date: Mon, 16 Jun 2008 22:16:55 GMT\r\n
    #### c1  | X-Varnish: 422080121\r\n
    #### c1  | Age: 0\r\n
    #### c1  | Via: 1.1 varnish\r\n
    #### c1  | Connection: keep-alive\r\n
    #### c1  | \r\n
    #### c1  http[ 0] | HTTP/1.1
    #### c1  http[ 1] | 200
    #### c1  http[ 2] | Ok
    #### c1  http[ 3] | Content-Length: 9
    #### c1  http[ 4] | Date: Mon, 16 Jun 2008 22:16:55 GMT
    #### c1  http[ 5] | X-Varnish: 422080121
    #### c1  http[ 6] | Age: 0
    #### c1  http[ 7] | Via: 1.1 varnish
    #### c1  http[ 8] | Connection: keep-alive
    #### c1  EXPECT resp.status (200) == 200 (200) match
    ##  c1  ending
    ##  s1  Waiting for server
    #### v1  CLI TX| stop
    ### v1  CLI 200 <stop>
    #  TEST tests/b00000.vtc completed

If instead of 200 we had expected 201 with the line:::

  expect resp.status == 201

The output would have ended with:::

  #### c1  http[ 0] | HTTP/1.1
  #### c1  http[ 1] | 200
  #### c1  http[ 2] | Ok
  #### c1  http[ 3] | Content-Length: 9
  #### c1  http[ 4] | Date: Mon, 16 Jun 2008 22:26:35 GMT
  #### c1  http[ 5] | X-Varnish: 648043653 648043652
  #### c1  http[ 6] | Age: 6
  #### c1  http[ 7] | Via: 1.1 varnish
  #### c1  http[ 8] | Connection: keep-alive
  ---- c1  EXPECT resp.status (200) == 201 (201) failed

SEE ALSO
========

* varnishhist(1)
* varnishlog(1)
* varnishncsa(1)
* varnishstat(1)
* varnishtop(1)
* vcl(7)

HISTORY
=======

The varnishtest program was developed by Poul-Henning Kamp
⟨phk@phk.freebsd.dk⟩ in cooperation with Linpro AS. This manual page
was written by Stig Sandbeck Mathisen ⟨ssm@linpro.no⟩ using examples
by Poul-Henning Kamp ⟨phk@phk.freebsd.dk⟩.

COPYRIGHT
=========

This document is licensed under the same licence as Varnish
itself. See LICENCE for details.

* Copyright (c) 2007-2008 Linpro AS
* Copyright (c) 2010 Varnish Software AS
