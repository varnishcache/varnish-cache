.. _users_performance:

Varnish and Website Performance
===============================

This section is about tuning the performance of your Varnish server,
and about tuning the performance of your website using Varnish.

The section is split into two. One deals with the various tools and
functions of Varnish that you should be aware of and the other focuses
on the how to purge content out of your cache. Purging of content is
essential in a performance context because it allows you to extend the
*time-to-live* (TTL) of your cached objects. Having a long TTL allows
Varnish to keep the content in cache longer, meaning Varnish will make
send fewer requests to your relativly slow backend.

.. toctree::
   :maxdepth: 2

   increasing-your-hitrate
   purging
