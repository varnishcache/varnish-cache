#!/usr/bin/env sh

set -eux

apk add -q --no-progress --update autoconf automake build-base ca-certificates gzip libedit-dev libtool libunwind-dev linux-headers pcre-dev py-docutils py3-sphinx tar alpine-sdk openssh-client ncurses-dev python

echo "PARAM_RELEASE: $PARAM_RELEASE"
echo "PARAM_DIST: $PARAM_DIST"

if [ -z "$PARAM_RELEASE" ]; then
    echo "Env variable PARAM_RELEASE is not set! For example PARAM_RELEASE=8, for CentOS 8"
    exit 1
elif [ -z "$PARAM_DIST" ]; then
    echo "Env variable PARAM_DIST is not set! For example PARAM_DIST=centos"
    exit 1
fi

mkdir -p /package && cd /package
tar xazf /workspace/alpine.tar.gz --strip 1

adduser -D builder
echo "builder ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers
addgroup builder abuild
mkdir -p /var/cache/distfiles
chmod -R a+w /var/cache/distfiles

echo "Generate key"
su builder -c "abuild-keygen -nai"

echo "Fix APKBUILD's variables"
tar xavf /workspace/varnish-*.tar.gz
VERSION=$(varnish-*/configure --version | awk 'NR == 1 {print $NF}')
echo "Version: $VERSION"
sed -i "s/@VERSION@/$VERSION/" APKBUILD
rm -rf varnish-*/

echo "Fix checksums, build"
cp /workspace/varnish-*.tar.gz .
chown builder -R /workspace
chown builder -R /package

su builder -c "abuild checksum"
su builder -c "abuild -r"

echo "Fix the APKBUILD's version"
su builder -c "mkdir apks"
ARCH=`uname -m`
echo "Arch: $ARCH"
su builder -c "cp /home/builder/packages/$ARCH/*.apk apks"
ls -laR apks

echo "Import the packages into the workspace"
mkdir -p /packages/$PARAM_DIST/$PARAM_RELEASE/$ARCH/
mv /home/builder/packages/$ARCH/*.apk /packages/$PARAM_DIST/$PARAM_RELEASE/$ARCH/
