.. _reference-vsl:

=====================
Shared Memory Logging
=====================

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

Which in practice could look like::

	U F E 182 159 80 80 1392
	G F E 159 173 80 1304 1314
