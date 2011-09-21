#

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = include lib bin man etc doc

SUBDIRS += redhat

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = varnishapi.pc

EXTRA_DIST = LICENSE autogen.sh varnishapi.pc.in

DISTCHECK_CONFIGURE_FLAGS = \
    --enable-developer-warnings \
    --enable-debugging-symbols \
    --enable-dependency-tracking \
    --enable-diagnostics \
    --enable-extra-developer-warnings \
    --enable-tests \
    --enable-werror

install-data-local:
	$(install_sh) -d -m 0755 $(DESTDIR)$(localstatedir)/varnish

distcheck-hook:
	V="@PACKAGE_VERSION@" ; \
	V="$${V%%-*}" ; \
	if ! grep "^Version: $$V$$" $(top_distdir)/redhat/varnish.spec && \
	  [ "@PACKAGE_VERSION@" != "trunk" ]; then \
	  echo "$$V" ; \
	  echo "redhat/varnish.spec does not have a version number matching the package" ; \
	  exit 1 ; \
	fi

distcleancheck_listfiles = \
	find . -type f -exec sh -c 'test -f $(srcdir)/$$1 || echo $$1' \
		sh '{}' ';'
