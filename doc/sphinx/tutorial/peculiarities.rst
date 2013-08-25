
Peculiarities
-------------

There are a couple of things that are different with Varnish Cache, as
opposed to other programs. One thing you've already seen - VCL. I'll
just give you a very quick tour of the other pecularities.

Configuration
~~~~~~~~~~~~~

The Varnish Configuration is written in VCL. When Varnish is ran this
configuration is transformed into C code and then fed into a C
compiler, loaded and run. So, as opposed to declaring various
settings, you write polices on how the incomming traffic should be
handled.


varnishadm
~~~~~~~~~~

Varnish Cache has an admin console. You can connect it it through the
"varnishadm" command. In order to connect the user needs to be able to
read /etc/varnish/secret in order to authenticate.

Once you've started the console you can do quite a few operations on
Varnish, like stopping and starting the cache process, load VCL,
adjust the built in load balancer and invalidate cached content.

It has a built in command "help" which will give you some hints on
what it does.

varnishlog
~~~~~~~~~~

Varnish does not log to disk. Instead it logs to a bit of memory. It
is like a stream of logs. At any time you'll be able to connect to the
stream and see what is going on. Varnish logs quite a bit of
information. You can have a look at the logstream with the command
``varnishlog``.




