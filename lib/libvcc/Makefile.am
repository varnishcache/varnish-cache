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

dist_noinst_SCRIPTS = \
	generate.py

dist_pkgdata_SCRIPTS = \
	vmodtool.py \
	vsctool.py

## keep in sync with include/Makefile.am
vcc_obj.c: \
	    $(top_srcdir)/lib/libvcc/generate.py \
	    $(top_srcdir)/include/vcc_interface.h \
	    $(top_srcdir)/include/vdef.h \
	    $(top_srcdir)/include/vrt.h \
	    $(top_srcdir)/doc/sphinx/reference/vcl_var.rst
	mkdir -p $(top_builddir)/include/tbl
	@PYTHON@ $(top_srcdir)/lib/libvcc/generate.py \
	    $(top_srcdir) $(top_builddir)

## this list is not complete, but it contains important files
## used as includes and the last file built by generate.py
GEN_H = \
	vcc_fixed_token.c \
	vcc_token_defs.h \
	tbl/vrt_stv_var.h

$(GEN_H): vcc_obj.c

GENERATED_H = vcc_obj.c $(GEN_H)

BUILT_SOURCES = $(GENERATED_H)

MAINTAINERCLEANFILES = $(GENERATED_H)
