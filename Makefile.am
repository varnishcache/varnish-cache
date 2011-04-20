#

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = include lib bin man etc doc

SUBDIRS += redhat

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = varnishapi.pc

EXTRA_DIST = LICENSE autogen.sh varnishapi.pc.in

install-data-local:
	$(install_sh) -d -m 0755 $(DESTDIR)$(localstatedir)/varnish

distcheck-hook:
	if ! grep "^Version: @PACKAGE_VERSION@$$" $(top_distdir)/redhat/varnish.spec && \
	  [ "@PACKAGE_VERSION@" != "trunk" ]; then \
	  echo "redhat/varnish.spec does not have a version number matching the package" ; \
	  exit 1 ; \
	fi

distcleancheck_listfiles = \
	find . -type f -exec sh -c 'test -f $(srcdir)/$$1 || echo $$1' \
		sh '{}' ';'
