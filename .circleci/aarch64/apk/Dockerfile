FROM multiarch/alpine:aarch64-edge

USER root

RUN apk update
RUN apk add -q autoconf automake build-base ca-certificates gzip libedit-dev libtool libunwind-dev linux-headers pcre-dev py-docutils py3-sphinx tar alpine-sdk openssh-client ncurses-dev python

ADD make-apk-packages.sh /usr/bin/

CMD ["make-apk-packages.sh"]
