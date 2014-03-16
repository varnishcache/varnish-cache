

Parameters
----------

Varnish Cache comes with a set of parameters that affects behaviour and
performance. Most of these parameters can be set on the Varnish
command line (through `varnishadm`) using the ``param.set`` keyword.

Some parameters can, for security purposes be read only using the '-r'
command line switch to `varnishd`.

We don't recommend that you tweak parameters unless you're sure of what
you're doing. We've worked hard to make the defaults sane and Varnish
should be able to handle most workloads with the default settings.

For a complete listing of all the parameters and a short descriptions
type ``param.show`` in the CLI. To inspect a certain parameter and get
a somewhat longer description on what it does and what the default is
type ``param.show`` and the name of the parameter, like this::

  varnish> param.show shortlived
  200        
  shortlived                  10.000000 [s]
                              Default is 10.0
                              Objects created with TTL shorter than this are
                              always put in transient storage.


