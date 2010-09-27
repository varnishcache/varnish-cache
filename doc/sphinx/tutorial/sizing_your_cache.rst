
Sizing your cache
-----------------

Picking how much memory you should give Varnish can be a tricky
task. A few things to consider:
 * How big is your *hot* data set. For a portal or news site that
   would be the size of the front page with all the stuff on it, and
   the size of all the pages and objects linked from the first page. 
 * How expensive is it to generate an object? Sometimes it makes sense
   to only cache images a little while or not to cache them at all if
   they are cheap to serve from the backend and you have a limited
   amount of memory.
 * Watch the n_lru_nuked counter with varnishstat or some other
   tool. If you have a lot of LRU activity then you should consider
   increasing the size of the cache.
