#

AM_LDFLAGS  = $(AM_LT_LDFLAGS)

AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_builddir)/include

pkglib_LTLIBRARIES = libvcc.la

libvcc_la_LDFLAGS = $(AM_LDFLAGS) -avoid-version

libvcc_la_SOURCES = \
	vcc_compile.h \
	vcc_token_defs.h \
	\
	vcc_acl.c \
	vcc_action.c \
	vcc_backend.c \
	vcc_backend_util.c \
	vcc_compile.c \
	vcc_expr.c \
	vcc_parse.c \
	vcc_fixed_token.c \
	vcc_obj.c \
	vcc_storage.c \
	vcc_utils.c \
	vcc_symb.c \
	vcc_token.c \
	vcc_var.c \
	vcc_vmod.c \
	vcc_xref.c

EXTRA_DIST = \
	generate.py

dist_pkgdata_SCRIPTS = \
	vmodtool.py

vcc_obj.c vcc_fixed_token.c vcc_token_defs.h: \
	$(srcdir)/generate.py $(top_srcdir)/include/vrt.h
	@PYTHON@ $(srcdir)/generate.py $(srcdir) $(top_builddir)

CLEANFILES = $(builddir)/vcc_token_defs.h \
	$(builddir)/vcc_fixed_token.c \
	$(builddir)/vcc_obj.c
