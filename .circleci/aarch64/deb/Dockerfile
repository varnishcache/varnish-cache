FROM multiarch/ubuntu-core:arm64-bionic

ENV DEBIAN_FRONTEND=noninteractive
ENV DEBCONF_NONINTERACTIVE_SEEN=true
RUN apt-get update 
RUN apt-get install -y autoconf automake build-essential graphviz libncurses-dev libtool dpkg-dev ca-certificates debhelper devscripts equivs make gcc pkg-config libunwind-dev python3-docutils python3-sphinx ncurses-dev libpcre3-dev libedit-dev libjemalloc-dev

ADD make-deb-packages.sh /usr/bin/

CMD ["make-deb-packages.sh"]
