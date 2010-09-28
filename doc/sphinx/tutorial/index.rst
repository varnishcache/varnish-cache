.. _tutorial-index:

%%%%%%%%%%%%%
Using Varnish
%%%%%%%%%%%%%

This tutorial is intended for system administrators managing Varnish
cache. The reader should know how to configure her web- or application
server and have basic knowledge of the HTTP protocol. The reader
should have Varnish up and running with the default configuration. 

Good luck.

TOC

 Getting Varnish Running
	backend_servers.rst
        starting_varnish.rst
	logging.rst
        sizing_your_cache.rst
        putting_varnish_on_port_80.rst
 The Varnish Configuration Language
	vcl.rst

 Tuning Varnish
        increasing_your_hitrate.rst
	advanced_backend_servers.rst
        handling_misbehaving_servers.rst

 Advanced topics
        advanced_topics.rst

Troubleshooting and getting help
	troubleshooting.rst

.. toctree:: 

	backend_servers.rst
        starting_varnish.rst
	logging.rst
        sizing_your_cache.rst
        putting_varnish_on_port_80.rst
	vcl.rst
        statistics.rst
        increasing_your_hitrate.rst
	advanced_backend_servers.rst
        handling_misbehaving_servers.rst
        advanced_topics.rst
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

