
Peculiarities
-------------

There are a couple of things that are different with Varnish Cache, as
opposed to other programs. One thing you've already seen - VCL. In this section we provide a very quick tour of other peculiarities you need to know about to get the most out of Varnish.

Configuration
~~~~~~~~~~~~~

The Varnish Configuration is written in VCL. When Varnish is ran this
configuration is transformed into C code and then fed into a C
compiler, loaded and executed.

.. XXX:Ran sounds strange above, maybe "is running" "is started" "executes"? benc

So, as opposed to switching various
settings on or off, you write polices on how the incoming traffic should be
handled.


varnishadm
~~~~~~~~~~

Varnish Cache has an admin console. You can connect it it through the
``varnishadm`` command. In order to connect the user needs to be able to
read `/etc/varnish/secret` in order to authenticate.

Once you've started the console you can do quite a few operations on
Varnish, like stopping and starting the cache process, load VCL,
adjust the built in load balancer and invalidate cached content.

It has a built in command "help" which will give you some hints on
what it does.

.. XXX:sample of the command here. benc

varnishlog
~~~~~~~~~~

Varnish does not log to disk. Instead it logs to a chunk of memory. It
is actually streaming the logs. At any time you'll be able to connect to the
stream and see what is going on. Varnish logs quite a bit of
information. You can have a look at the logstream with the command
``varnishlog``.




