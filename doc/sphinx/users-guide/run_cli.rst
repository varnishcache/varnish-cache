..
	Copyright (c) 2013-2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of licens

.. _run_cli:

CLI - bossing Varnish around
============================

Once ``varnishd`` is started, you can control it using the ``varnishadm``
program and the command line interface::

	varnishadm help

If you want to run ``varnishadm`` from a remote system, we recommend
you use ``ssh`` into the system where ``varnishd`` runs. (But see also:
:ref:`Local and remote CLI connections <ref_remote_cli>`)

You can SSH into the ``varnishd`` computer and run ``varnishadm``::

	ssh $hostname varnishadm help

If you give no command arguments, ``varnishadm`` runs in interactive mode
with command-completion, command-history and other comforts:

.. code-block:: text

    critter phk> ./varnishadm 
    200        
    -----------------------------
    Varnish Cache CLI 1.0
    -----------------------------
    FreeBSD,13.0-CURRENT,amd64,-jnone,-sdefault,-sdefault,-hcritbit
    varnish-trunk revision 2bd5d2adfc407216ebaa653fae882d3c8d47f5e1
    
    Type 'help' for command list.
    Type 'quit' to close CLI session.
    Type 'start' to launch worker process.
    
    varnish> 

The CLI always returns a three digit status code to tell how things went.

200 and 201 means *OK*, anything else means that some kind of trouble
prevented the execution of the command.

(If you get 201, it means that the output was truncated,
See the :ref:`ref_param_cli_limit` parameter.)

When commands are given as arguments to ``varnishadm``, a status
different than 200 or 201 will cause it to exit with status 1
and print the status code on standard error.

What can you do with the CLI
----------------------------

From the CLI you can:

* load/use/discard VCL programs
* ban (invalidate) cache content
* change parameters
* start/stop worker process

We will discuss each of these briefly below.

Load, use and discard VCL programs
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

All caching and policy decisions are made by VCL programs.

You can have multiple VCL programs loaded, but one of them
is designated the "active" VCL program, and this is where
all new requests start out.

To load new VCL program::

	varnish> vcl.load some_name some_filename

Loading will read the VCL program from the file, and compile it. If
the compilation fails, you will get an error messages:

.. code-block:: text

	.../mask is not numeric.
	('input' Line 4 Pos 17)
			"192.168.2.0/24x",
	----------------#################-

	Running VCC-compiler failed, exit 1
	VCL compilation failed

If compilation succeeds, the VCL program is loaded, and you can
now make it the active VCL, whenever you feel like it::

	varnish> vcl.use some_name

If you find out that was a really bad idea, you can switch back
to the previous VCL program again::

	varnish> vcl.use old_name

The switch is instantaneous, all new requests will start using the
VCL you activated right away. The requests currently being processed complete
using whatever VCL they started with.

We highly recommend you design an emergency-VCL, and always keep
it loaded, so it can be activated with ::

	vcl.use emergency

Ban cache content
^^^^^^^^^^^^^^^^^

Varnish offers "purges" to remove things from cache, but that
requires you to know exactly what they are.

Sometimes it is useful to be able to throw things out of cache
without having an exact list of what to throw out.

Imagine for instance that the company logo changed and now you need
Varnish to stop serving the old logo out of the cache:

.. code-block:: text

	varnish> ban req.url ~ "logo.*[.]png"

should do that, and yes, that is a regular expression.

We call this "banning" because the objects are still in the cache,
but they are now banned from delivery, while all the rest of the
cache is unaffected.

Even when you want to throw out *all* the cached content, banning is
both faster and less disruptive that a restart::

	varnish> ban obj.http.date ~ .*

.. In addition to handling such special occasions, banning can be used
.. in many creative ways to keep the cache up to date, more about
.. that in: (TODO: xref)


Change parameters
^^^^^^^^^^^^^^^^^

Parameters can be set on the command line with the '-p' argument,
but almost all parameters can be examined and changed on the fly
from the CLI:

.. code-block:: text

	varnish> param.show prefer_ipv6
	200
	prefer_ipv6         off [bool]
                            Default is off
                            Prefer IPv6 address when connecting to backends
                            which have both IPv4 and IPv6 addresses.

	varnish> param.set prefer_ipv6 true
	200

In general it is not a good idea to modify parameters unless you
have a good reason, such as performance tuning or security configuration.

.. XXX: Natural delay of some duration sounds vague. benc

Most parameters will take effect instantly, or with a short delay,
but a few of them requires you to restart the child process before
they take effect. This is always mentioned in the description of
the parameter.

Starting and stopping the worker process
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

In general you should just leave the worker process running, but
if you need to stop and/or start it, the obvious commands work::

	varnish> stop

and::

	varnish> start

If you start ``varnishd`` with the '-d' (debugging) argument, you will
always need to start the child process explicitly.

Should the child process die, the master process will automatically
restart it, but you can disable that with the
:ref:`ref_param_auto_restart` parameter.
