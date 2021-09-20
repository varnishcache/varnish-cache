#

TESTS = @VTC_TESTS@

include $(top_srcdir)/vtc.am

DISTCLEANFILES = _.ok

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/lib/libvgz

bin_PROGRAMS =	varnishtest

varnishtest_SOURCES = \
		hpack.h \
		cmds.h \
		vtc.h \
		teken.c \
		teken.h \
		teken_scs.h \
		teken_subr.h \
		teken_subr_compat.h \
		teken_wcwidth.h \
		vtc.c \
		vtc_barrier.c \
		vtc_client.c \
		vtc_gzip.c \
		vtc_haproxy.c \
		vtc_h2_dectbl.h \
		vtc_h2_enctbl.h \
		vtc_h2_hpack.c \
		vtc_h2_priv.h \
		vtc_h2_stattbl.h \
		vtc_h2_tbl.c \
		vtc_http.c \
		vtc_http.h \
		vtc_http2.c \
		vtc_log.h \
		vtc_log.c \
		vtc_logexp.c \
		vtc_misc.c \
		vtc_main.c \
		vtc_process.c \
		vtc_proxy.c \
		vtc_server.c \
		vtc_sess.c \
		vtc_subr.c \
		vtc_syslog.c \
		vtc_tunnel.c \
		vtc_varnish.c

varnishtest_LDADD = \
		$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
		$(top_builddir)/lib/libvarnish/libvarnish.la \
		$(top_builddir)/lib/libvgz/libvgz.la \
		${PTHREAD_LIBS} ${NET_LIBS} ${LIBM}

varnishtest_CFLAGS = \
		-DVTEST_WITH_VTC_LOGEXPECT \
		-DVTEST_WITH_VTC_VARNISH \
		-DTOP_BUILDDIR='"${top_builddir}"'

EXTRA_DIST = $(top_srcdir)/bin/varnishtest/tests/*.vtc \
	$(top_srcdir)/bin/varnishtest/tests/README \
	$(top_srcdir)/bin/varnishtest/gensequences \
	$(top_srcdir)/bin/varnishtest/sequences \
	$(top_srcdir)/bin/varnishtest/teken.3 \
	huffman_gen.py

teken.c: teken_state.h

teken_state.h:	$(srcdir)/sequences $(srcdir)/gensequences
	awk -f $(srcdir)/gensequences $(srcdir)/sequences \
	    > $(builddir)/teken_state.h

vtc_h2_hpack.c: vtc_h2_dectbl.h
vtc_h2_dectbl.h: huffman_gen.py $(top_srcdir)/include/tbl/vhp_huffman.h
	$(PYTHON) $(srcdir)/huffman_gen.py \
	    $(top_srcdir)/include/tbl/vhp_huffman.h > $@_
	mv $@_ $@

BUILT_SOURCES = vtc_h2_dectbl.h

CLEANFILES = \
	$(builddir)/teken_state.h	\
	$(BUILT_SOURCES)
