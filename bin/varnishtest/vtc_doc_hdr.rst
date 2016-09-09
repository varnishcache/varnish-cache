Syntax RFP for HTTP2 in varnishtest
===================================

This document tries to document how varnishtest would work this H/2, adapting to
the introduction of stream. The main idea is the introduction of a new command
(stream) inside client and server specification that naively maps to RFC7540.

It provides little abstraction, allowwing great control over test scenario,
while still retaining a familiar logic.

Here's an example of test file::

        server s1 {
        	non-fatal
        	stream 1 {
        		rxreq
        		expect req.http.foo == <undef>
        		txgoaway -laststream 0 -err 9 -debug "COMPRESSION_ERROR"
        	} -run
        } -start

        client c1 -connect ${s1_sock} {
        	stream 1 {
        		txreq -idxHdr 100 -litHdr inc plain "foo" plain "bar"
        		rxgoaway
        		expect goaway.err == 9
        		expect goaway.laststream == 0
        		expect goaway.debug == "COMPRESSION_ERROR"
        	} -run
        } -run

.. contents::

