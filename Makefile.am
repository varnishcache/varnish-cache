ACLOCAL_AMFLAGS = -I m4 -I .

SUBDIRS = include lib bin etc doc man

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = varnishapi.pc

m4dir = $(datadir)/aclocal
m4_DATA = varnish.m4 varnish-legacy.m4

CLEANFILES = \
	cscope.in.out \
	cscope.out \
	cscope.po.out \
	witness.dot \
	witness.svg

EXTRA_DIST = \
	README.rst \
	README.Packaging \
	LICENSE \
	autogen.sh \
	varnishapi.pc.in \
	varnish.m4 \
	varnish-legacy.m4 \
	vtc.am

AM_DISTCHECK_CONFIGURE_FLAGS = \
    --enable-developer-warnings \
    --enable-debugging-symbols \
    --enable-dependency-tracking \
    CFLAGS="$(EXTCFLAGS)"

if WITH_UNWIND
AM_DISTCHECK_CONFIGURE_FLAGS += --with-unwind
endif

if WITH_LINUX_JAIL
AM_DISTCHECK_CONFIGURE_FLAGS += --with-linux-jail
endif

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

# XXX: This is a similar hack to ensure we have a built varnishtest
# (and technically any other binary that may be involved in a VTC)
# before we try to run tests anywhere in the tree.
check-recursive: all

cscope:
	-rm -f cscope*
	find . -name '*.[hcS]' > cscope.files
	cscope -b

gcov_digest:
	${PYTHON} tools/gcov_digest.py -o _gcov

witness.dot: all
	$(MAKE) check AM_VTC_LOG_FLAGS=-pdebug=+witness
	$(AM_V_GEN) $(srcdir)/tools/witness.sh witness.dot bin/varnishtest/ \
		lib/libvmod_*/

.dot.svg:
if ! HAVE_DOT
	@echo ==================================================
	@echo You need graphviz installed to generate svg output
	@echo ==================================================
	@false
else
	$(AM_V_GEN) $(DOT) -Tsvg $< >$@
endif

if HAVE_DOT
witness: witness.svg
else
witness: witness.dot
endif

.PHONY: cscope witness.dot
