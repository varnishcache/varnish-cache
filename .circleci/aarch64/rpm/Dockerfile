FROM multiarch/centos:7-aarch64-clean

RUN yum install -y epel-release automake jemalloc-devel git libedit-devel libtool libunwind-devel make pcre-devel python3  python-sphinx rpm-build yum-utils

ADD make-rpm-packages.sh /usr/bin/

CMD ["make-rpm-packages.sh"]
