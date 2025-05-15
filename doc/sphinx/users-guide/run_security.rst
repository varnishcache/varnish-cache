..
	Copyright (c) 2013-2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _run_security:

Security first
==============

If you are the only person involved in running Varnish, or if all
the people involved are trusted to the same degree, you can skip
this chapter. We have protected Varnish as well as we can from
anything which can come in through an HTTP socket.

If parts of your web infrastructure are outsourced or otherwise
partitioned along administrative lines, you need to think about
security.

Varnish provides four levels of authority, roughly related to
how and where control comes into Varnish:

  * The command line arguments,

  * The CLI interface,

  * VCL programs, and

  * HTTP requests.

Command line arguments
----------------------

The top level security decisions is decided and defined when starting
Varnish in the form of command line arguments, we use this strategy
in order to make them invulnerable to subsequent manipulation.

The important decisions to make are:

#. Who should have access to the Command Line Interface?

#. Which parameters can they change?

#. Will inline-C code be allowed?

#. If/how VMODs will be restricted?

#. How child processes will be jailed?

CLI interface access
^^^^^^^^^^^^^^^^^^^^

The command line interface can be accessed in three ways.

:ref:`varnishd(1)` can be told to listen and offer CLI connections on
a TCP socket. You can bind the socket to pretty much anything the
kernel will accept::

	-T 127.0.0.1:631
	-T localhost:9999
	-T 192.168.1.1:34
	-T '[fe80::1]:8082'

The default is ``-T localhost:0`` which will pick a random port
number, which :ref:`varnishadm(1)` can learn from the shared memory.

By using a ``localhost`` address, you restrict CLI access to the local
machine.

You can also bind the CLI port to an IP address reachable across
the net, and let other machines connect directly.

This gives you no secrecy, i.e. the CLI commands will go across the
network as ASCII text with no encryption, but the ``-S`` / pre shared
key (`PSK`_) authentication requires the remote end to know the shared
secret.

Alternatively you can bind the CLI port to a ``localhost`` address,
and give remote users access via a secure connection to the local
machine, using ssh/VPN or similar.

If you use `ssh(1)` you can restrict which commands each user can
execute to just :ref:`varnishadm(1)`, or even use a wrapper scripts
around :ref:`varnishadm(1)` to allow specific CLI commands.

It is also possible to configure :ref:`varnishd(1)` for "reverse
mode", using the ``-M`` argument.  In that case :ref:`varnishd(1)`
will attempt to open a TCP connection to the specified address, and
initiate a CLI connection to your central Varnish management facility.

.. XXX:Maybe a sample command here with a brief explanation? benc

The connection in this case is also without encryption, but
the remote end must still authenticate using ``-S``\ /`PSK`_.

Finally, if you run varnishd with the ``-d`` option, you get a CLI
command on stdin/stdout, but since you started the process, it would
be hard to prevent you getting CLI access, wouldn't it ?

.. _PSK:

CLI interface authentication
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

By default the CLI interface is protected with a simple, yet powerful
"Pre Shared Key" authentication method, which do not provide secrecy
(ie: The CLI commands and responses are not encrypted).

The way ``-S``\ /PSK works is really simple: During startup a file is
created with a random content and the file is only accessible to the
user who started :ref:`varnishd(1)` (or the superuser).

To authenticate and use a CLI connection, you need to know the
contents of that file, in order to answer the cryptographic challenge
:ref:`varnishd(1)` issues, see :ref:`ref_psk_auth`.

:ref:`varnishadm(1)` uses all of this to restrict access, it will only
function, provided it can read the secret file.

If you want to allow other users, local or remote, to be able to
access CLI connections, you must create your own secret file and make
it possible for (only!) these users to read it.

A good way to create the secret file is::

	dd if=/dev/random of=/etc/varnish_secret count=1

When you start :ref:`varnishd(1)`, you specify the filename with '-S',
and it goes without saying that the :ref:`varnishd(1)` master process
needs to be able to read the file too.

You can change the contents of the secret file while
:ref:`varnishd(1)` runs, it is read every time a CLI connection is
authenticated.

On the local system, :ref:`varnishadm(1)` can retrieve the filename
from shared memory, but on remote systems, you need to give
:ref:`varnishadm(1)` a copy of the secret file, with the -S argument.

If you want to disable ``-S``\ /PSK authentication, use an ``-S none``
argument to varnishd::

	varnishd [...] -S none [...]

Parameters
^^^^^^^^^^

Parameters can be set from the command line, and made "read-only"
(using '-r') so they cannot subsequently be modified from the CLI
interface.

Pretty much any parameter can be used to totally mess up your
HTTP service, but a few can do more damage than others:

:ref:`ref_param_cc_command`
	Execute arbitrary programs

:ref:`ref_param_vcc_feature`
	The ``allow_inline_c`` flag would allow any C code from VCL to be
	executed by Varnish.

Furthermore you may want to look at and lock down:

:ref:`ref_param_syslog_cli_traffic`
	Log all CLI commands to `syslog(8)`, so you know what goes on.

:ref:`ref_param_vcc_feature` again
	The ``unsafe_path`` flag allows VCL/VMODs to be loaded outside of
	:ref:`ref_param_vcl_path` and :ref:`ref_param_vmod_path`.

:ref:`ref_param_vmod_path`
	The directory (or colon separated list of directories) where
	Varnish will look for modules. This could potentially be
	used to load rogue modules into Varnish.

The CLI interface
-----------------

The CLI interface in Varnish is very powerful, if you have
access to the CLI interface, you can do almost anything to
the Varnish process.

As described above, some of the damage can be limited by restricting
certain parameters, but that will only protect the local filesystem,
and operating system, it will not protect your HTTP service.

We do not currently have a way to restrict specific CLI commands to
specific CLI connections. One way to get such an effect is to "wrap"
all CLI access in pre-approved scripts which use :ref:`varnishadm(1)`
to submit the sanitized CLI commands, and restrict a remote user to
only those scripts, for instance using sshd(8)'s configuration.

VCL programs
------------

There are two "dangerous" mechanisms available in VCL code:  VMODs
and inline-C.

Both of these mechanisms allow execution of arbitrary code and will
thus allow a person to get access to the machine, with the
privileges of the child process.

If :ref:`varnishd(1)` is started as root/superuser, we sandbox the
child process, using whatever facilities are available on the
operating system, but if :ref:`varnishd(1)` is not started as
root/superuser, this is not possible. No, don't ask me why you have to
be superuser to lower the privilege of a child process...

.. XXX the above is not correct for the solaris jail

Inline-C is disabled by default since Varnish version 4, so unless
you enable it, you don't have to worry about it.

The parameters mentioned above can restrict the loading of VMODs to only
be loaded from a designated directory, restricting VCL wranglers to a
pre-approved subset of VMODs.

If you do that, we are confident that your local system cannot be compromised
from VCL code.

HTTP requests
-------------

We have gone to great lengths to make Varnish resistant to anything
coming in through the socket where HTTP requests are received, and
you should, generally speaking, not need to protect it any further.

The caveat is that since VCL is a programming language which lets you
decide exactly what to do with HTTP requests, you can also decide
to do stupid and potentially dangerous things with them, including opening yourself up
to various kinds of attacks and subversive activities.

If you have "administrative" HTTP requests, for instance PURGE
requests, we strongly recommend that you restrict them to trusted
IP numbers/nets using VCL's :ref:`vcl_syntax_acl`.
