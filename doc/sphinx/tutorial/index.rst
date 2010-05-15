.. _tutorial-index:

%%%%%%%%%%%%%%%%
Varnish Tutorial
%%%%%%%%%%%%%%%%

.. toctree::

	intro.rst
	backend_servers.rst
        starting_varnish.rst
	logging.rst
        putting_varnish_on_port_80.rst
	vcl.rst
        statistics.rst
        increasing_your_hitrate.rst
	advanced_backend_servers.rst
	troubleshooting.rst

.. todo::
        starting varnish with -d, seeing a transaction go through
        explain varnishlog output for a miss and a hit
        a few simple VCL tricks, including switching VCL on the fly
        The helpers: varnishstat, varnishhist, varnishtop varnishncsa
        Now that you know how it works, lets talk planning:
        - backend, directors and polling
        - storage
        - logging
        - management CLI & security
        - ESI
        Real life examples:
        - A real life varnish explained
        - A more complex real life varnish explained
        - Sky's Wikia Setup
        Varnishtest
        - What varnishtest does and why
        - writing simple test-cases
        - using varnishtest to test your VCL
        - using varnishtest to reproduce bugs

