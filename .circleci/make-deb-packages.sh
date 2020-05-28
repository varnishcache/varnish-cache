#!/usr/bin/env bash

set -eux

export DEBIAN_FRONTEND=noninteractive
export DEBCONF_NONINTERACTIVE_SEEN=true
apt-get update
apt-get install -y dpkg-dev debhelper devscripts equivs pkg-config apt-utils fakeroot

echo "PARAM_RELEASE: $PARAM_RELEASE"
echo "PARAM_DIST: $PARAM_DIST"


if [ -z "$PARAM_RELEASE" ]; then
    echo "Env variable PARAM_RELEASE is not set! For example PARAM_RELEASE=8, for CentOS 8"
    exit 1
elif [ -z "$PARAM_DIST" ]; then
    echo "Env variable PARAM_DIST is not set! For example PARAM_DIST=centos"
    exit 1
fi

# Ubuntu 20.04 aarch64 fails when using fakeroot-sysv with:
#    semop(1): encountered an error: Function not implemented
update-alternatives --set fakeroot /usr/bin/fakeroot-tcp

cd /varnish-cache
ls -la

echo "Untar debian..."
tar xavf debian.tar.gz

echo "Untar orig..."
tar xavf varnish-*.tar.gz --strip 1

echo "Update changelog version..."
if [ -e .is_weekly ]; then
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
mkdir -p packages/$PARAM_DIST/$PARAM_RELEASE/
mv ../*.deb packages/$PARAM_DIST/$PARAM_RELEASE/

if [ "`uname -m`" = "x86_64" ]; then
  ARCH="amd64"
else
  ARCH="arm64"
fi

DSC_FILE=$(ls ../*.dsc)
DSC_FILE_WO_EXT=$(basename ${DSC_FILE%.*})
mv $DSC_FILE packages/$PARAM_DIST/$PARAM_RELEASE/${DSC_FILE_WO_EXT}_${ARCH}.dsc
