.. _run_security:

Security first
==============

If you are the only person involved in running Varnish, or if all
the people involved are trusted to the same degree, you can skip
this chapter:  We have protected Varnish as well as we can from
anything which can come in through HTTP socket.

If parts of your web infrastructure are outsourced or otherwise
partitioned along adminitrative lines, you need to think about
security.

Varnish provides four levels of authority, roughly related to
how and where the command comes into Varnish:

  * The command line arguments

  * The CLI interface

  * VCL programs

  * HTTP requests

Command line arguments
----------------------

The top level security decisions is taken on and from the command
line, in order to make them invulnerable to subsequent manipulation.

The important decisions to make are:

#. Who should have access to the Command Line Interface ?

#. Which parameters can they change ?

#. Will inline-C code be allowed ?

#. If/how VMODs will be restricted ?

CLI interface access
^^^^^^^^^^^^^^^^^^^^

The most important of these is the CLI interface:  Should it be
accessible only on the local machine, or should it be accessible
also from across the network ?

No matter what you do, you should always protect the CLI with a
Pre-Shared-Key (The -S argument).

The way -S/PSK works is really simple:  You specify -S and filename,
and only somebody who knows the exact contents of that file can
access the CLI.  By protecting the secret file with suitable UNIX
permissions, you can restrict CLI access

The varnishadm(8) program knows how to do the -S/PSK protocol,
both locally and remote.

(XXX ref: user-guide: setting up -S)

The CLI port is a TCP socket, and it can be bound to any IP
number+socket combination the kernel will accept::

	-T 127.0.0.1:631
	-T localhost:9999
	-T 192.168.1.1:34
	-T '[fe80::1]:8082'

If you want to be able to use the CLI remotely, you can do it
two ways.

You can bind the CLI port to a reachable IP number, and connect
directly.  This gives you no secrecy, ie, the CLI commands will
go across the network as ASCII text with no encryption, but
using the -S option, you will get authentication.

Alternatively you can bind the CLI port to a 'localhost' address,
and give remote users access via a secure connection to the local
machine (ssh, VPN, etc. etc.)

It is also possible to configure varnishd for "reverse mode", using
the '-M' argument,

In this case varnishd will attempt to open a TCP connection to the
specified address, and initiate a CLI connection on it.

The connection is also in this case without secrecy, but if configured
the remote end must still satisfy -S/PSK authentication.

Parameters
^^^^^^^^^^

Parameters can be set from the command line, and made "read-only"
(using -r) so they cannot subsequently be modified from the CLI
interface.

Pretty much any parameter can be used to totally mess up your
HTTP service, but a few can do more damage than that::

	user			-- access to local system via VCL
	group			-- access to local system via VCL
	listen_address		-- trojan other service ports (ssh!)
	cc_command		-- execute arbitrary programs

Furthermore you may want to look at::

	syslog_cli_traffic	-- know what is going on
	vcc_unsafe_path		-- retrict VCL/VMODS to vcl_dir+vmod_dir
	vcl_dir			-- VCL include dir
	vmod_dir		-- VMOD import dir

The CLI interface
-----------------

The CLI interface in Varnish is very powerful, if you have
access to the CLI interface, you can do almost anything to
the Varnish process.

As described above, some of the damage can be limited by restricting
certain parameters, but that will only protect the local filesystem,
and operating system, it will not protect your HTTP service.

We do not currently have a way to restrict specific CLI commands
to specific CLI connections.   One way to get such an effect is to
"wrap" all CLI access in pre-approved scripts which use varnishadm(1)
to submit the sanitized CLI commands, and restrict a remote user
to only those scripts in sshd(8)'s configuration.

VCL programs
------------

There are two "dangerous" mechanisms available in VCL code:  VMODs
and inline-C.

Both of these mechanisms allow execution of arbitrary code and will
therefore allow a person to get access on the computer, with the
privileges of the child process.

If varnishd is started as root/superuser, we sandbox the child
process, using whatever facilities are available on the operating
system, but if varnishd is not started as root/superuser, this is
not possible.  No, don't ask me why you have to be superuser to
lower the privilege of a child process...

Inline-C is disabled by default starting with Varnish 4, so unless
you enable it, you don't have to worry about it.

The params mentioned above can restrict VMOD so they can only
be imported from a designated directory, restricting VCL wranglers
to a pre-approved subset of VMODs.

If you do that, we belive that your local system cannot be compromised
from VCL code.

HTTP requests
-------------

We have gone to great lengths to make Varnish resistant to anything
coming in throught he socket where HTTP requests are received, and
you should, generally speaking, not need to protect it any further.

The caveat is that since VCL is a programming language which lets you
decide exactly what to do about HTTP requests, you can also decide
to do exactly stupid things to them, including opening youself up
to various kinds of attacks and subversive activities.

If you have "administrative" HTTP requests, for instance PURGE
requests, we recommend that you restrict them to trusted IP
numbers/nets using VCL's Access Control Lists.

(XXX: missing ref to ACL)
