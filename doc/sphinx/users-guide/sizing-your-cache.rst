
Sizing your cache
-----------------

Deciding on cache size can be a tricky task. A few things to consider:

 * How big is your *hot* data set. For a portal or news site that
   would be the size of the front page with all the stuff on it, and
   the size of all the pages and objects linked from the first page.

 * How expensive is it to generate an object? Sometimes it makes sense
   to only cache images a little while or not to cache them at all if
   they are cheap to serve from the backend and you have a limited
   amount of memory.

 * Watch the `n_lru_nuked` counter with :ref:`ref-varnishstat` or
   some other tool. If you have a lot of LRU activity then your cache
   is evicting objects due to space constraints and you should
   consider increasing the size of the cache.

Be aware that every object that is stored also carries overhead that
is kept outside the actually storage area. So, even if you specify ``-s
malloc,16G`` Varnish might actually use **double** that. Varnish has a
overhead of about 1KB per object. So, if you have lots of small objects
in your cache the overhead might be significant.

.. XXX:This seems to contradict the last paragraph in "storage-backends". benc
