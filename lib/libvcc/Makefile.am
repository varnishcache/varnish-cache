#

AM_LDFLAGS  = $(AM_LT_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include

noinst_LIBRARIES = libvcc.a

libvcc_a_CFLAGS = \
	@SAN_CFLAGS@

libvcc_a_SOURCES = \
	vcc_compile.h \
	vcc_token_defs.h \
	vcc_acl.c \
	vcc_action.c \
	vcc_backend.c \
	vcc_backend_util.c \
	vcc_compile.c \
	vcc_expr.c \
	vcc_fixed_token.c \
	vcc_obj.c \
	vcc_parse.c \
	vcc_storage.c \
	vcc_symb.c \
	vcc_token.c \
	vcc_types.c \
	vcc_utils.c \
	vcc_var.c \
	vcc_vmod.c \
	vcc_xref.c

EXTRA_DIST = \
	generate.py

dist_pkgdata_SCRIPTS = \
	vmodtool.py \
	vsctool.py

vcc_obj.c vcc_fixed_token.c vcc_token_defs.h: \
	$(top_builddir)/include/vcl.h

CLEANFILES = $(builddir)/vcc_token_defs.h \
	$(builddir)/vcc_fixed_token.c \
	$(builddir)/vcc_obj.c
