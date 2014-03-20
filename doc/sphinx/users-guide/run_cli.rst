.. _run_cli:

CLI - bossing Varnish around
============================

Once `varnishd` is started, you can control it using the command line
interface.

The easiest way to do this, is using `varnishadm` on the
same machine as `varnishd` is running::

	varnishadm help

If you want to run `varnishadm` from a remote system, you can do it
two ways.

You can SSH into the `varnishd` computer and run `varnishadm`::

	ssh $http_front_end varnishadm help

But you can also configure `varnishd` to accept remote CLI connections
(using the '-T' and '-S' arguments)::

	varnishd -T :6082 -S /etc/varnish_secret

And then on the remote system run `varnishadm`::

	varnishadm -T $http_front_end -S /etc/copy_of_varnish_secret help

but as you can see, SSH is much more convenient.

If you run `varnishadm` without arguments, it will read CLI commands from
`stdin`, if you give it arguments, it will treat those as the single
CLI command to execute.

The CLI always returns a status code to tell how it went:  '200'
means OK, anything else means there were some kind of trouble.

`varnishadm` will exit with status 1 and print the status code on
standard error if it is not 200.

What can you do with the CLI
----------------------------

The CLI gives you almost total control over `varnishd` some of the more important tasks you can perform are:

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
the compilation fails, you will get an error messages::

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

It is good idea to design an emergency-VCL before you need it,
and always have it loaded, so you can switch to it with a single
vcl.use command.

.. XXX:Should above have a clearer admonition like a NOTE:? benc

Ban cache content
^^^^^^^^^^^^^^^^^

Varnish offers "purges" to remove things from cache, provided that
you know exactly what they are.

But sometimes it is useful to be able to throw things out of cache
without having an exact list of what to throw out.

Imagine for instance that the company logo changed and now you need
Varnish to stop serving the old logo out of the cache::

	varnish> ban req.url ~ "logo.*[.]png"

should do that, and yes, that is a regular expression.

We call this "banning" because the objects are still in the cache,
but they are banned from delivery.

Instead of checking each and every cached object right away, we
test each object against the regular expression only if and when
an HTTP request asks for it.

Banning stuff is much cheaper than restarting Varnish to get rid
of wronly cached content.

.. In addition to handling such special occasions, banning can be used
.. in many creative ways to keep the cache up to date, more about
.. that in: (TODO: xref)


Change parameters
^^^^^^^^^^^^^^^^^

Parameters can be set on the command line with the '-p' argument,
but they can also be examined and changed on the fly from the CLI::

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

Most parameters will take effect instantly, or with a natural delay
of some duration,

.. XXX: Natural delay of some duration sounds vague. benc

but a few of them requires you to restart the
child process before they take effect. This is always noted in the
description of the parameter.

Starting and stopping the worker process
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

In general you should just leave the worker process running, but
if you need to stop and/or start it, the obvious commands work::

	varnish> stop

and::

	varnish> start

If you start `varnishd` with the '-d' (debugging) argument, you will
always need to start the child process explicitly.

Should the child process die, the master process will automatically
restart it, but you can disable that with the 'auto_restart' parameter.
