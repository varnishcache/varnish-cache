.. _users-guide-separate_VCL:

Separate VCL files
==================

Having multiple different vhosts in the same Varnish is a very
typical use-case, and from Varnish 5.0 it is possible to have
a separate VCL files for separate vhosts or any other distinct
subset of requests.

Assume that we want to handle ``varnish.org`` with one VCL file
and ``varnish-cache.org`` with another VCL file.

First load the two VCL files::

    vcl.load vo_1 /somewhere/vo.vcl
    vcl.load vc_1 /somewhere/vc.vcl

These are 100% normal VCL files, as they would look if you ran
only that single domain on your varnish instance.

Next we need to point VCL labels to them::

    vcl.label l_vo vo_1
    vcl.label l_vc vc_1

Next we write the top-level VCL program, which branches out
to the other two, depending on the Host: header in the
request::

    import std;

    # We have to have a backend, even if we do not use it
    backend default { .host = "127.0.0.1"; }

    sub vcl_recv {
	# Normalize host header
	set req.http.host = std.tolower(req.http.host);

	if (req.http.host ~ "\.?varnish\.org$") {
	    return (vcl(l_vo));
	}
	if (req.http.host ~ "\.?varnish-cache\.org$") {
	    return (vcl(l_vc));
	}
	return (synth(302, "http://varnish-cache.org"));
    }

    sub vcl_synth {
	if (resp.status == 301 || resp.status == 302) {
	    set resp.http.location = resp.reason;
	    set resp.reason = "Moved";
	    return (deliver);
	}
    }

Finally, we load the top level VCL and make it the
active VCL::

    vcl.load top_1 /somewhere/top.vcl
    vcl.use top_1

If you want to update one of the separated VCLs, you load the new
one and change the label to point to it::

    vcl.load vo_2 /somewhere/vo.vcl
    vcl.label l_vo vo_2

If you want to change the top level VCL, do as you always did::

    vcl.load top_2 /somewhere/top.vcl
    vcl.use top_2



Details, details, details:
--------------------------

* All requests *always* start in the active VCL - the one from ``vcl.use``

* Only VCL labels can be used in ``return(vcl(name))``.  Without this
  restriction the top level VCL would have to be reloaded every time
  one of the separate VCLs were changed.

* You can only switch VCLs from the active VCL.  If you try it from one of
  the separate VCLs, you will get a 503

* You cannot remove VCL labels (with ``vcl.discard``) if any VCL
  contains ``return(vcl(name_of_that_label))``

* You cannot remove VCLs which have a label attached to them.

* This code is tested in testcase c00077

* This is a very new feature, it may change

* We would very much like feedback how this works for you
