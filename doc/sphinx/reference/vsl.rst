.. _reference-vsl:

=====================
Shared Memory Logging
=====================

TTL records
~~~~~~~~~~~

A TTL record is emitted whenever the ttl, grace or keep values for an
object is set.

The format is::

	%u %s %d %d %d %d %d [ %d %u %u ]
	|  |  |  |  |  |  |    |  |  |
	|  |  |  |  |  |  |    |  |  +- Max-Age from Cache-Control header
	|  |  |  |  |  |  |    |  +---- Expires header
	|  |  |  |  |  |  |    +------- Date header
	|  |  |  |  |  |  +------------ Age (incl Age: header value)
	|  |  |  |  |  +--------------- Reference time for TTL
	|  |  |  |  +------------------ Keep
	|  |  |  +--------------------- Grace
	|  |  +------------------------ TTL
	|  +--------------------------- "RFC" or "VCL"
	+------------------------------ object XID

The last three fields are only present in "RFC" headers.

Examples::

	1001 RFC 19 -1 -1 1312966109 4 0 0 23
	1001 VCL 10 -1 -1 1312966109 4
	1001 VCL 7 -1 -1 1312966111 6
	1001 VCL 7 120 -1 1312966111 6
	1001 VCL 7 120 3600 1312966111 6
	1001 VCL 12 120 3600 1312966113 8

Gzip records
~~~~~~~~~~~~

A Gzip record is emitted for each instance of gzip or gunzip work
performed.
Worst case, an ESI transaction stored in gzip'ed objects but delivered
gunziped, will run into many of these.

The format is::

	%c %c %c %d %d %d %d %d
	|  |  |  |  |  |  |  |
	|  |  |  |  |  |  |  +- Bit length of compressed data
	|  |  |  |  |  |  +---- Bit location of 'last' bit
	|  |  |  |  |  +------- Bit location of first deflate block
	|  |  |  |  +---------- Bytes output
	|  |  |  +------------- Bytes input
	|  |  +---------------- 'E' = ESI, '-' = Plain object
	|  +------------------- 'F' = Fetch, 'D' = Deliver
	+---------------------- 'G' = Gzip, 'U' = Gunzip, 'u' = Gunzip-test

Examples::

	U F E 182 159 80 80 1392
	G F E 159 173 80 1304 1314
