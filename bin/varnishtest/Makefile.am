#

TESTS = @VTC_TESTS@

include $(top_srcdir)/vtc.am

DISTCLEANFILES = _.ok

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_builddir) \
	-I$(top_srcdir)/lib/libvgz \
	-I$(srcdir)/vtest2/lib \
	-I$(srcdir)/vtest2/src

bin_PROGRAMS =	varnishtest

# for -i invocation / a00000.vtc
all-local: vtest

vtest: varnishtest
	ln -f varnishtest vtest

install-exec-hook:
	ln -f $(DESTDIR)$(bindir)/varnishtest$(EXEEXT) \
	   $(DESTDIR)$(bindir)/vtest$(EXEEXT)

uninstall-hook:
	rm -f $(DESTDIR)$(bindir)/vtest$(EXEEXT)

varnishtest_SOURCES = \
		vtest2/src/hpack.h \
		vtest2/src/cmds.h \
		vtest2/src/vtc.h \
		vtest2/src/teken.c \
		vtest2/src/teken.h \
		vtest2/src/teken_scs.h \
		vtest2/src/teken_subr.h \
		vtest2/src/teken_subr_compat.h \
		vtest2/src/teken_wcwidth.h \
		vtest2/src/vtc.c \
		vtest2/src/vtc_barrier.c \
		vtest2/src/vtc_client.c \
		vtest2/src/vtc_gzip.c \
		vtest2/src/vtc_haproxy.c \
		vtest2/src/vtc_h2_enctbl.h \
		vtest2/src/vtc_h2_hpack.c \
		vtest2/src/vtc_h2_priv.h \
		vtest2/src/vtc_h2_stattbl.h \
		vtest2/src/vtc_h2_tbl.c \
		vtest2/src/vtc_http.c \
		vtest2/src/vtc_http.h \
		vtest2/src/vtc_http2.c \
		vtest2/src/vtc_log.h \
		vtest2/src/vtc_log.c \
		vtest2/src/vtc_logexp.c \
		vtest2/src/vtc_misc.c \
		vtest2/src/vtc_main.c \
		vtest2/src/vtc_process.c \
		vtest2/src/vtc_proxy.c \
		vtest2/src/vtc_server.c \
		vtest2/src/vtc_sess.c \
		vtest2/src/vtc_subr.c \
		vtest2/src/vtc_syslog.c \
		vtest2/src/vtc_tunnel.c \
		vtest2/src/vtc_varnish.c \
		vtest2/src/vtc_vsm.c \
		vtest2/src/vtest_api.h \
		vtest2/lib/vudp.h \
		vtest2/lib/vudp.c

varnishtest_LDADD = \
		$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
		$(top_builddir)/lib/libvarnish/libvarnish.la \
		$(top_builddir)/lib/libvgz/libvgz.la \
		${PTHREAD_LIBS} ${NET_LIBS} ${LIBM} -ldl

varnishtest_CFLAGS = \
		-DVTEST_WITH_VTC_LOGEXPECT \
		-DVTEST_WITH_VTC_VARNISH \
		-DVTEST_WITH_VTC_VSM \
		-DTOP_BUILDDIR='"${top_builddir}"'

EXTRA_DIST = $(srcdir)/vtest2/src/../tests/*.vtc \
	$(top_srcdir)/bin/varnishtest/tests/*.vtc \
	$(top_srcdir)/bin/varnishtest/tests/common.pem \
	$(top_srcdir)/bin/varnishtest/tests/README \
	vtest2/src/gensequences \
	vtest2/src/sequences \
	vtest2/src/teken.3 \
	vtest2/src/huffman_gen.py \
	vtest2/src/tbl/vhp_huffman.h

teken.c: teken_state.h

teken_state.h:	$(srcdir)/vtest2/src/sequences $(srcdir)/vtest2/src/gensequences
	awk -f $(srcdir)/vtest2/src/gensequences $(srcdir)/vtest2/src/sequences \
	    > $@_
	mv $@_ $@

vtc_h2_hpack.c: vtc_h2_dectbl.h
vtc_h2_dectbl.h: $(srcdir)/vtest2/src/huffman_gen.py $(srcdir)/vtest2/src/tbl/vhp_huffman.h
	$(PYTHON) $(srcdir)/vtest2/src/huffman_gen.py \
	    $(srcdir)/vtest2/src/tbl/vhp_huffman.h > $@_
	mv $@_ $@

BUILT_SOURCES = \
	teken_state.h \
	vtc_h2_dectbl.h

CLEANFILES = \
	$(BUILT_SOURCES)		\
	vtest
