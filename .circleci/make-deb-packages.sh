#!/usr/bin/env bash

set -eux

export DEBIAN_FRONTEND=noninteractive
export DEBCONF_NONINTERACTIVE_SEEN=true
apt-get update
apt-get install -y autoconf automake build-essential graphviz libncurses-dev libtool dpkg-dev ca-certificates debhelper devscripts equivs make gcc pkg-config libunwind-dev python3-docutils python3-sphinx ncurses-dev libpcre3-dev libedit-dev libjemalloc-dev apt-utils

echo "PARAM_RELEASE: $PARAM_RELEASE"
echo "PARAM_DIST: $PARAM_DIST"


if [ -z "$PARAM_RELEASE" ]; then
    echo "Env variable PARAM_RELEASE is not set! For example PARAM_RELEASE=8, for CentOS 8"
    exit 1
elif [ -z "$PARAM_DIST" ]; then
    echo "Env variable PARAM_DIST is not set! For example PARAM_DIST=centos"
    exit 1
fi


adduser --disabled-password --gecos "" varnish

chown -R varnish:varnish /workspace

DIST_DIR=build
rm -rf $DIST_DIR
mkdir -p $DIST_DIR
cd $DIST_DIR

echo "Untar debian..."
tar xavf /workspace/debian.tar.gz

echo "Untar orig..."
tar xavf /workspace/varnish-*.tar.gz --strip 1

echo "Update changelog version..."
if [ -e /workspace/.is_weekly ]; then
    WEEKLY='-weekly'
else
    WEEKLY=
fi
VERSION=$(./configure --version | awk 'NR == 1 {print $NF}')$WEEKLY~$PARAM_RELEASE
sed -i -e "s|@VERSION@|$VERSION-1|"  "debian/changelog"

echo "Install Build-Depends packages..."
yes | mk-build-deps --install debian/control || true

echo "Build the packages..."
dpkg-buildpackage -us -uc -j16

echo "Prepare the packages for storage..."
mkdir -p /packages/$PARAM_DIST/$PARAM_RELEASE/
mv ../*.deb /packages/$PARAM_DIST/$PARAM_RELEASE/
