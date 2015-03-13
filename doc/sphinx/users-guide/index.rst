.. _users-guide-index:

The Varnish Users Guide
=======================

The Varnish documentation consists of three main documents:

* :ref:`tutorial-index` explains the basics and gets you started with Varnish.

* :ref:`users-guide-index` (this document), explains how Varnish works
  and how you can use it to improve your website. 

* :ref:`reference-index` contains hard facts and is useful for
  looking up specific questions.

After :ref:`users_intro`, this Users Guide is organized in sections
following the major interfaces to Varnish as a service:

:ref:`users_running` is about getting Varnish configured, with
respect to storage, sockets, security and how you can control and
communicate with Varnish once it is running.

:ref:`users_vcl` is about getting Varnish to handle the
HTTP requests the way you want, what to cache, how to cache it,
modifying HTTP headers etc. etc.

:ref:`users_report` explains how you can monitor what Varnish does,
from a transactional level to aggregating statistics.

:ref:`users_performance` is about tuning your website with Varnish.

:ref:`users_trouble` is for locating and fixing common issues with Varnish.

.. toctree::
   :maxdepth: 2

   intro
   running
   vcl
   report
   performance
   esi
   troubleshooting

.. customizing (which is a non ideal title)

.. No longer used:

        configuration
        command_line
        VCL
	backend_servers
	logging
        sizing_your_cache
        statistics
        increasing_your_hitrate
	cookies
	vary
        hashing
	purging
	compression
	esi
	websockets
	devicedetection
        handling_misbehaving_servers
        advanced_topics
	troubleshooting

