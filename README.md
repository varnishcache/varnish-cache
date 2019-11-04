Varnish Cache
=============

[![CircleCI Build Status](https://circleci.com/gh/varnishcache/varnish-cache/tree/master.svg?style=svg)](https://circleci.com/gh/varnishcache/varnish-cache/tree/master)
[![TravisCI Build Status](https://travis-ci.org/varnishcache/varnish-cache.svg?branch=master)](https://travis-ci.org/varnishcache/varnish-cache)

This is Varnish Cache, the high-performance HTTP accelerator.

Documentation and additional information about Varnish is available
on https://www.varnish-cache.org/

Technical questions about Varnish and this release should be addressed
to <varnish-misc@varnish-cache.org>.

Please see CONTRIBUTING for how to contribute patches and report bugs.

Questions about commercial support and services related to Varnish
should be addressed to <sales@varnish-software.com>.

More platforms are tested via [vtest](https://varnish-cache.org/vtest/).

## Packaging

Varnish Cache packaging files are kept outside of the main distribution.

The main reason for this is to decouple the development work from the packaging work.

We want to be able to tag a release and do a tarball release without having
to wait for the packagers to finish their work/changes.

### Official packages

The official Debian and Redhat packages are built by the Varnish
Cache team and made available on https://packagecloud.io/varnishcache/.

Packaging files and scripts for Debian and Redhat:

    https://github.com/varnishcache/pkg-varnish-cache

### Third-party packages

Varnish Cache is built and packaged in many different operating
systems and distributions. Please see https://varnish-cache.org/
for more information.
