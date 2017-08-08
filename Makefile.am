ACLOCAL_AMFLAGS = -I m4 -I .

SUBDIRS = include lib bin etc doc man

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = varnishapi.pc

m4dir = $(datadir)/aclocal
m4_DATA = varnish.m4 varnish-legacy.m4

CLEANFILES = cscope.in.out cscope.out cscope.po.out
EXTRA_DIST = \
	README.rst \
	README.Packaging \
	LICENSE \
	autogen.sh \
	varnishapi.pc.in \
	varnish.m4 \
	varnish-legacy.m4

DISTCHECK_CONFIGURE_FLAGS = \
    --enable-developer-warnings \
    --enable-debugging-symbols \
    --enable-dependency-tracking \
    CFLAGS="$(EXTCFLAGS)"

install-data-local:
	$(install_sh) -d -m 0755 $(DESTDIR)$(localstatedir)/varnish


distcleancheck_listfiles = \
	find . -type f -exec sh -c 'test -f $(srcdir)/$$1 || echo $$1' \
		sh '{}' ';'

vtest-clean:
	$(am__remove_distdir)

# XXX: This is a hack to ensure we have a built source tree when
# running make dist If we had used non-recursive make we could have
# solved it better, but we don't, so use this at least for now.
LICENSE: all

cscope:
	-rm -f cscope*
	find . -name '*.[hcS]' > cscope.files
	cscope -b

gcov_digest:
	${PYTHON} tools/gcov_digest.py -o _gcov

.PHONY: cscope
