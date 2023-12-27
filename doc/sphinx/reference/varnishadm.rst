..
	Copyright (c) 2010-2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. role:: ref(emphasis)

.. _varnishadm(1):

==========
varnishadm
==========

Control a running Varnish instance
----------------------------------

:Manual section: 1

SYNOPSIS
========

varnishadm [-e] [-h] [-n ident] [-p] [-S secretfile] [-T [address]:port] [-t timeout] [command [...]]


DESCRIPTION
===========

The `varnishadm` utility establishes a CLI connection to varnishd either
using -n *name* or using the -T and -S arguments. If -n *name* is
given the location of the secret file and the address:port is looked
up in shared memory. If neither is given `varnishadm` will look for an
instance without a given name.

If a command is given, the command and arguments are sent over the CLI
connection and the result returned on stdout.

If no command argument is given `varnishadm` will pass commands and
replies between the CLI socket and stdin/stdout.

OPTIONS
=======

-e
    Exit immediately if a command fails in `pass` mode with an exit 
    status indicating the nature of the error (See EXIT STATUS for details).
    This has no effect on `interactive` mode (except when `-p` is used).
    Similarly to `CLI Command File` (see :ref:`varnishd(1)`), if a command
    is prefixed with '-', its failure will be ignored and will not cause
    varnishadm to exit.

-h
    Print program usage and exit.

-n ident
    Connect to the instance of `varnishd` with this name.

-p
    Force `pass` mode and make the output follow the VCLI protocol.
    This disables command-history/command-completion and makes it
    easier for programs to parse the response(s).

-S secretfile
    Specify the authentication secret file. This should be the same -S
    argument as was given to `varnishd`. Only processes which can read
    the contents of this file, will be able to authenticate the CLI connection.

-T <address:port>
    Connect to the management interface at the specified address and port.

-t timeout
    Wait no longer than this many seconds for an operation to finish.


The syntax and operation of the actual CLI interface is described in
the :ref:`varnish-cli(7)` manual page. Parameters are described in
:ref:`varnishd(1)` manual page.

Additionally, a summary of commands can be obtained by issuing the
*help* command, and a summary of parameters can be obtained by issuing
the *param.show* command.

EXIT STATUS
===========

If a command is given, the exit status of the `varnishadm` utility is
zero if the command succeeded, and one of the following otherwise:

1: could not execute the command
2: bad usage
3: the command failed
4: the connection was lost
5: the CLI session was terminated

EXAMPLES
========

Some ways you can use varnishadm::

   varnishadm -T localhost:999 -S /var/db/secret vcl.use foo
   echo vcl.use foo | varnishadm -T localhost:999 -S /var/db/secret
   echo vcl.use foo | ssh vhost varnishadm -T localhost:999 -S /var/db/secret

SEE ALSO
========

* :ref:`varnishd(1)`
* :ref:`varnish-cli(7)`

AUTHORS
=======

The `varnishadm` utility and this manual page were written by Cecilie
Fritzvold. This man page has later been modified by Per Buer, Federico G.
Schwindt and Lasse Karstensen.
