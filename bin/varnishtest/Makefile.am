#

TESTS = @VTC_TESTS@

include $(top_srcdir)/vtc.am

DISTCLEANFILES = _.ok

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_builddir) \
	-I$(top_srcdir)/lib/libvgz

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

vtest2_src = vtest2/src

varnishtest_SOURCES = \
		$(vtest2_src)/hpack.h \
		$(vtest2_src)/cmds.h \
		$(vtest2_src)/vtc.h \
		$(vtest2_src)/teken.c \
		$(vtest2_src)/teken.h \
		$(vtest2_src)/teken_scs.h \
		$(vtest2_src)/teken_subr.h \
		$(vtest2_src)/teken_subr_compat.h \
		$(vtest2_src)/teken_wcwidth.h \
		$(vtest2_src)/vtc.c \
		$(vtest2_src)/vtc_barrier.c \
		$(vtest2_src)/vtc_client.c \
		$(vtest2_src)/vtc_gzip.c \
		$(vtest2_src)/vtc_haproxy.c \
		$(vtest2_src)/vtc_h2_enctbl.h \
		$(vtest2_src)/vtc_h2_hpack.c \
		$(vtest2_src)/vtc_h2_priv.h \
		$(vtest2_src)/vtc_h2_stattbl.h \
		$(vtest2_src)/vtc_h2_tbl.c \
		$(vtest2_src)/vtc_http.c \
		$(vtest2_src)/vtc_http.h \
		$(vtest2_src)/vtc_http2.c \
		$(vtest2_src)/vtc_log.h \
		$(vtest2_src)/vtc_log.c \
		$(vtest2_src)/vtc_logexp.c \
		$(vtest2_src)/vtc_misc.c \
		$(vtest2_src)/vtc_main.c \
		$(vtest2_src)/vtc_process.c \
		$(vtest2_src)/vtc_proxy.c \
		$(vtest2_src)/vtc_server.c \
		$(vtest2_src)/vtc_sess.c \
		$(vtest2_src)/vtc_subr.c \
		$(vtest2_src)/vtc_syslog.c \
		$(vtest2_src)/vtc_tunnel.c \
		$(vtest2_src)/vtc_varnish.c \
		$(vtest2_src)/vtc_vsm.c

varnishtest_LDADD = \
		$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
		$(top_builddir)/lib/libvarnish/libvarnish.la \
		$(top_builddir)/lib/libvgz/libvgz.la \
		${PTHREAD_LIBS} ${NET_LIBS} ${LIBM}

varnishtest_CFLAGS = \
		-DVTEST_WITH_VTC_LOGEXPECT \
		-DVTEST_WITH_VTC_VARNISH \
		-DVTEST_WITH_VTC_VSM \
		-DTOP_BUILDDIR='"${top_builddir}"'

EXTRA_DIST = $(srcdir)/$(vtest2_src)/../tests/*.vtc \
	$(top_srcdir)/bin/varnishtest/tests/*.vtc \
	$(top_srcdir)/bin/varnishtest/tests/common.pem \
	$(top_srcdir)/bin/varnishtest/tests/README \
	$(vtest2_src)/gensequences \
	$(vtest2_src)/sequences \
	$(vtest2_src)/teken.3 \
	$(vtest2_src)/huffman_gen.py \
	$(vtest2_src)/tbl/vhp_huffman.h

teken.c: teken_state.h

teken_state.h:	$(srcdir)/$(vtest2_src)/sequences $(srcdir)/$(vtest2_src)/gensequences
	awk -f $(srcdir)/$(vtest2_src)/gensequences $(srcdir)/$(vtest2_src)/sequences \
	    > $@_
	mv $@_ $@

vtc_h2_hpack.c: vtc_h2_dectbl.h
vtc_h2_dectbl.h: $(srcdir)/$(vtest2_src)/huffman_gen.py $(srcdir)/$(vtest2_src)/tbl/vhp_huffman.h
	$(PYTHON) $(srcdir)/$(vtest2_src)/huffman_gen.py \
	    $(srcdir)/$(vtest2_src)/tbl/vhp_huffman.h > $@_
	mv $@_ $@

BUILT_SOURCES = \
	teken_state.h \
	vtc_h2_dectbl.h

CLEANFILES = \
	$(BUILT_SOURCES)		\
	vtest
