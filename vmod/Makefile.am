#

TESTS = @VMOD_TESTS@

include $(top_srcdir)/vtc.am

AM_LDFLAGS  = $(AM_LT_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/bin/varnishd \
	-I$(top_builddir)/include

#
# Declare vmod sources
#

libvmod_blob_la_SOURCES = \
	vmod_blob.c \
	vmod_blob.h \
	id.c \
	base64.c \
	hex.h \
	hex.c \
	url.c \
	tbl_encodings.h \
	tbl_case.h

libvmod_cookie_la_SOURCES = \
	vmod_cookie.c

.vsc.c:
	$(PYTHON) $(top_srcdir)/lib/libvcc/vsctool.py -c $<

.vsc.h:
	$(PYTHON) $(top_srcdir)/lib/libvcc/vsctool.py -h $<

libvmod_debug_extra_sources = \
	VSC_debug.vsc

libvmod_debug_built_sources = \
	VSC_debug.c \
	VSC_debug.h

libvmod_debug_la_SOURCES = \
	vmod_debug.c \
	vmod_debug_obj.c \
	vmod_debug_dyn.c

libvmod_directors_la_SOURCES = \
	vdir.c \
	vdir.h \
	fall_back.c \
	hash.c \
	misc.c \
	random.c \
	round_robin.c \
	vmod_shard.c \
	shard_cfg.c \
	shard_cfg.h \
	shard_dir.c \
	shard_dir.h

libvmod_proxy_la_SOURCES = \
	vmod_proxy.c

libvmod_purge_la_SOURCES = \
	vmod_purge.c

libvmod_std_la_SOURCES = \
	vmod_std.c \
	vmod_std_conversions.c \
	vmod_std_fileread.c \
	vmod_std_querysort.c

libvmod_unix_la_SOURCES = \
	vmod_unix.c \
	cred_compat.h

libvmod_vtc_la_SOURCES = \
	vmod_vtc.c

#
# Prepare build variables
#

vmod_LTLIBRARIES =
EXTRA_DIST = $(libvmod_debug_extra_sources) $(TESTS)
CLEANFILES = $(libvmod_debug_built_sources)
BUILT_SOURCES = $(libvmod_debug_built_sources)

#
# Use vmodtool.py generated automake boilerplate
#

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

nodist_libvmod_debug_la_SOURCES += $(libvmod_debug_built_sources)

# Allow Vmod_wrong*_Data to be exported
vmod_debug_symbols_regex = 'Vmod_.*_Data'

# not --strict
vmodtoolargs_debug = --boilerplate -o vcc_debug_if
