#

TESTS = @VMOD_TESTS@

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

vmod_LTLIBRARIES =

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

.vsc.c:
	$(PYTHON) $(top_srcdir)/lib/libvsc/vsctool.py -c $<

.vsc.h:
	$(PYTHON) $(top_srcdir)/lib/libvsc/vsctool.py -h $<

EXTRA_DIST += VSC_debug.vsc
nodist_libvmod_debug_la_SOURCES += \
	VSC_debug.c \
	VSC_debug.h

vmod_debug.c: VSC_debug.c VSC_debug.h

# Allow Vmod_wrong*_Data to be exported
vmod_debug_symbols_regex = 'Vmod_.*_Data'

# not --strict
vmodtoolargs_debug = --boilerplate -o vcc_debug_if
