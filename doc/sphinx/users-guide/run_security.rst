.. _run_security:

Security first
==============

If you are the only person involved in running Varnish, or if all
the people involved are trusted to the same degree, you can skip
this chapter.

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

The top level security decisions is taken from the command line,
in order to make them invulnerable to subsequent manipulation.

The important decisions to make are:

#. Who should have access to the Command Line Interface ?

#. Which parameters can the change ?

#. Will inline-C code be allowed ?

#. Will VMODs be restricted.


The most important of these is the CLI interface:  Should it be
accessible only on the local machine, or should it be accessible
also from across the network ?

No matter what you do, you should always protect the CLI with a
Pre-Shared-Key (The -S argument).

The way -S/PSK works is really simple:  You specify -S and filename,
and only somebody who knows what is in that file can access the CLI.

They do not need to be able to read that specific file on that
specific machine, as long as they know *exactly* what is in that file.

If the CLI should only be available on the local machine, bind the
CLI port to a loopback IP number ("-T 127.0.0.1").

If you want to be able to use the CLI remotely, you can do it
two ways:

You can bind the CLI port to a reachable IP number, and connect
directly.  This gives you no secrecy, the CLI commands will
go across the network as ASCII test with no encryption.

Or you can bind the CLI port locally, and give remote users access
via a secure connection to the local machine (ssh, VPN, etc. etc.)

It is also possible to configure varnishd for "reverse mode", where
varnishd will attempt to open a TCP connection to a specified
address, and initiate a CLI connection on it.  This is meant to
make it easier to manage a cluster of varnish machines from a single
"cluster controller" process.

Other parameters to consider are the uid/gid of the child process.

The CLI interface
-----------------

The CLI interface in Varnish is very powerful, if you have
access to the CLI interface, you can do almost anything to
the varnish process.

Some restrictions can be put in place from the command line arguments,
for instance specific parameters can be made Read-Only with the -r
argument, which prevents changes to them from the CLI.

We do not currently have a way to restrict specific CLI commands
to specific CLI connections.  (One way to get such an effect is to
not "wrap" all CLI access in pre-approved scripts which use
varnishadm(1) to submit the sanitized CLI commands, and restrict a
remote user to only those scripts in sshd(8)'s configuration.)

VCL programs
------------

There are two "dangerous" mechnisms available in VCL code:  VMODs
and inline-C.

Both of these mechanisms allow execution of arbitrary code and will
therefore allow a person to get shell access on the computer.
(XXX: doc which privs)

Inline C can be disabled with the parameter "vcc_allow_inline_c",
remember to make it read-only from the commandline if you don't
trust the CLI wranglers to leave it alone.

VMODs can be restricted to be loaded only from the path specified
using parameter 'vmod_dir' using the 'vcc_unsafe_path' parameter.
Again: remember to make the read-only.

If you do this, we belive that the integrity of the machine
running varnishd, cannot be compromised from the VCL program,
but it will be possible to read files available to the child
process.

HTTP requests
-------------

We have gone to great lengths to make varnish resistant to anything
coming in throught he socket where HTTP requests are received,
but given that VCL is a programming language which lets you
decide exactly what to do about HTTP requests, you can also decide
to do stupid things to them.

VCL offers a IP based Access-Control-List facility which allows you
to restrict certain requests, for instance PURGE, to certain IP
numbers/ranges.

