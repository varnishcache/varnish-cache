#

TESTS = @VMOD_TESTS@

include $(top_srcdir)/vsc.am
include $(top_srcdir)/vtc.am

EXTRA_DIST = $(TESTS)

AM_LDFLAGS  = $(AM_LT_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/bin/varnishd \
	-I$(top_builddir)/include

#
# Use vmodtool.py generated automake boilerplate
#

vccdir=$(datarootdir)/$(PACKAGE)/vcc

vmod_LTLIBRARIES =
vmod_vcc_files =

include $(srcdir)/automake_boilerplate_blob.am
include $(srcdir)/automake_boilerplate_cookie.am
include $(srcdir)/automake_boilerplate_debug.am
include $(srcdir)/automake_boilerplate_directors.am
include $(srcdir)/automake_boilerplate_proxy.am
include $(srcdir)/automake_boilerplate_purge.am
include $(srcdir)/automake_boilerplate_std.am
include $(srcdir)/automake_boilerplate_unix.am
include $(srcdir)/automake_boilerplate_vtc.am

#
# Post-boilerplate tweaks
#

VSC_SRC = VSC_debug.vsc

libvmod_debug_la_SOURCES += $(VSC_SRC)

BUILT_SOURCES = $(VSC_GEN)

# Allow Vmod_wrong*_Data to be exported
vmod_debug_symbols_regex = 'Vmod_.*_Data'

# not --strict
vmodtoolargs_debug = --boilerplate -o vcc_debug_if

# unregister VCC file installation
dist_noinst_DATA = $(srcdir)/vmod_debug.vcc
vmod_debug_vcc =

dist_vcc_DATA = $(vmod_vcc_files)
