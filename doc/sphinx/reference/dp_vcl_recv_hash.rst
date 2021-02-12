.. _db_vcl_recv_hash:

Hashing in `vcl_recv{}`
=======================

Calculating the `hash` used for cache lookup already in `vcl_recv{}`
makes it possible for certain directors to offer targeted health status.

To ensure consistent hashing, use this design pattern::

    sub make_hash_key {
        hash_data([…]);
    }
    
    sub vcl_hash {
        call make_hash_key;
        return (lookup);
    }

    sub vcl_recv {
        […]
        call make_hash_key;
        […]
    }
