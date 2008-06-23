# $Id$

SUBDIRS = include lib bin man etc doc

SUBDIRS += redhat

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = varnishapi.pc

EXTRA_DIST = LICENSE autogen.sh varnishapi.pc.in

install-data-local:
	$(install_sh) -d -m 0755 $(DESTDIR)$(localstatedir)/varnish
