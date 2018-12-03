#

VTC_LOG_COMPILER = ./varnishtest -v -i
TEST_EXTENSIONS = .vtc
TESTS = @VTC_TESTS@

vtest = $(srcdir)/vtest/src

# Make sure we run check-local first
check-am: check-local
# See if list of checks have changed, recheck
check-local:
	@mkdir -p tests ; \
	LC_ALL=C; \
	if [ "$$(cd $(srcdir) && echo tests/*.vtc)" != "@VTC_TESTS@" ]; then \
		cd $(top_builddir) && ./config.status --recheck ; \
	fi

DISTCLEANFILES = _.ok

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/lib/libvgz

bin_PROGRAMS =	varnishtest

varnishtest_SOURCES = \
		$(vtest)/hpack.h \
		$(vtest)/programs.h \
		$(vtest)/cmds.h \
		$(vtest)/vtc.h \
		$(vtest)/teken.c \
		$(vtest)/teken.h \
		$(vtest)/teken_scs.h \
		$(vtest)/teken_subr.h \
		$(vtest)/teken_subr_compat.h \
		$(vtest)/teken_wcwidth.h \
		$(vtest)/vtc.c \
		$(vtest)/vtc_barrier.c \
		$(vtest)/vtc_client.c \
		$(vtest)/vtc_haproxy.c \
		$(vtest)/vtc_h2_enctbl.h \
		$(vtest)/vtc_h2_hpack.c \
		$(vtest)/vtc_h2_priv.h \
		$(vtest)/vtc_h2_stattbl.h \
		$(vtest)/vtc_h2_tbl.c \
		$(vtest)/vtc_http.c \
		$(vtest)/vtc_http.h \
		$(vtest)/vtc_http2.c \
		$(vtest)/vtc_log.c \
		$(vtest)/vtc_logexp.c \
		$(vtest)/vtc_misc.c \
		$(vtest)/vtc_main.c \
		$(vtest)/vtc_process.c \
		$(vtest)/vtc_proxy.c \
		$(vtest)/vtc_server.c \
		$(vtest)/vtc_subr.c \
		$(vtest)/vtc_syslog.c \
		$(vtest)/vtc_varnish.c

# XXX does not belong to vtest
varnishtest_SOURCES += $(vtest)/vmods.h

varnishtest_LDADD = \
		$(top_builddir)/lib/libvarnish/libvarnish.a \
		$(top_builddir)/lib/libvarnishapi/libvarnishapi.la \
		$(top_builddir)/lib/libvgz/libvgz.a \
		@SAN_LDFLAGS@ \
		@PCRE_LIBS@ \
		${PTHREAD_LIBS} ${NET_LIBS} ${LIBM}

varnishtest_CFLAGS = \
		@SAN_CFLAGS@ \
		-DTOP_BUILDDIR='"${top_builddir}"'

EXTRA_DIST = $(top_srcdir)/bin/varnishtest/tests/*.vtc \
	$(top_srcdir)/bin/varnishtest/tests/README \
	$(top_srcdir)/bin/varnishtest/teken.3 \
	$(vtest)/gensequences \
	$(vtest)/sequences \
	$(vtest)/huffman_gen.py

$(vtest)/teken.c: teken_state.h

teken_state.h:	$(vtest)/sequences $(vtest)/gensequences
	awk -f $(vtest)/gensequences $(vtest)/sequences \
	    >teken_state.h

varnishtest_SOURCES += vtc_h2_dectbl.h
vtc_h2_hpack.c: vtc_h2_dectbl.h
vtc_h2_dectbl.h: $(vtest)/huffman_gen.py $(top_srcdir)/include/tbl/vhp_huffman.h
	$(PYTHON) $(vtest)/huffman_gen.py \
	    $(top_srcdir)/include/tbl/vhp_huffman.h > $@_
	mv $@_ $@

BUILT_SOURCES = vtc_h2_dectbl.h

CLEANFILES = \
	$(builddir)/teken_state.h	\
	$(BUILT_SOURCES)
