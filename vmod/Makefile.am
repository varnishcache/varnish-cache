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
# Declare vmod sources
#

libvmod_blob_la_SOURCES = \
	vmod_blob.c \
	vmod_blob.h \
	vmod_blob_id.c \
	vmod_blob_base64.c \
	vmod_blob_hex.c \
	vmod_blob_url.c \
	vmod_blob_tbl_encodings.h \
	vmod_blob_tbl_case.h

libvmod_cookie_la_SOURCES = \
	vmod_cookie.c

libvmod_debug_la_SOURCES = \
	vmod_debug.c \
	vmod_debug_obj.c \
	vmod_debug_dyn.c

libvmod_directors_la_SOURCES = \
	vmod_directors.c \
	vmod_directors.h \
	vmod_directors_fall_back.c \
	vmod_directors_hash.c \
	vmod_directors_random.c \
	vmod_directors_round_robin.c \
	vmod_directors_shard.c \
	vmod_directors_shard_cfg.c \
	vmod_directors_vmod_directors_shard_cfg.h \
	vmod_directors_shard_dir.c \
	vmod_directors_shard_dir.h

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
	vmod_unix_cred_compat.h

libvmod_vtc_la_SOURCES = \
	vmod_vtc.c

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
	$(PYTHON) $(top_srcdir)/lib/libvcc/vsctool.py -c $<

.vsc.h:
	$(PYTHON) $(top_srcdir)/lib/libvcc/vsctool.py -h $<

EXTRA_DIST += VSC_debug.vsc
nodist_libvmod_debug_la_SOURCES += \
	VSC_debug.c \
	VSC_debug.h

# Allow Vmod_wrong*_Data to be exported
vmod_debug_symbols_regex = 'Vmod_.*_Data'

# not --strict
vmodtoolargs_debug = --boilerplate -o vcc_debug_if
