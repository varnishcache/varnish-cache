#

TESTS = @VMOD_TESTS@

include $(top_srcdir)/vsc.am
include $(top_srcdir)/vtc.am

EXTRA_DIST = $(TESTS) vmod_vcs_version.txt

DISTCLEANFILES = vmod_vcs_version.txt

AM_LDFLAGS  = $(AM_LT_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/bin/varnishd \
	-I$(top_builddir)/include

#
# Use vmodtool.py generated automake boilerplate
#

vccdir=$(datarootdir)/$(PACKAGE)/vcc

# Prepare variables populated by includes
vmod_LTLIBRARIES =
noinst_LTLIBRARIES =
dist_vcc_DATA =
dist_noinst_DATA =

include $(srcdir)/automake_boilerplate_blob.am
include $(srcdir)/automake_boilerplate_h2.am
include $(srcdir)/automake_boilerplate_cookie.am
include $(srcdir)/automake_boilerplate_debug.am
include $(srcdir)/automake_boilerplate_directors.am
include $(srcdir)/automake_boilerplate_math.am
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
libvmod_debug_la_CFLAGS += -I$(top_srcdir)/lib/libvgz

BUILT_SOURCES = $(VSC_GEN)

# Allow Vmod_wrong*_Data to be exported
vmod_debug_symbols_regex = 'Vmod_.*_Data'

# not --strict, not installed
vmodtoolargs_debug = --boilerplate --noinst -o vcc_debug_if

### vmod_math specific
# the specs are deliberately checked in to not make the build
# dependend on external connectivity
vmod_math_spec.html:
	curl -o $@ https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/math.h.html

vmod_math.c: vmod_math.vcc

vmod_math.vcc: vmod_math_spec.html vmod_math_gen.py
	@PYTHON@ $(srcdir)/vmod_math_gen.py $< vmod_math.vcc vmod_math.c

# avoid running curl when building from a dist
dist_noinst_SCRIPTS = vmod_math_gen.py vmod_math_spec.html
