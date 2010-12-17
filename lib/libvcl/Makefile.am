# $Id$

INCLUDES = -I$(top_srcdir)/include -I$(top_builddir)/include

lib_LTLIBRARIES = libvcl.la

libvcl_la_LDFLAGS = -version-info 1:0:0

libvcl_la_SOURCES = \
	vcc_priv.h \
	vcc_compile.h \
	vcc_token_defs.h \
	vcc_types.h \
	symbol_kind.h \
	\
	vcc_acl.c \
	vcc_action.c \
	vcc_backend.c \
	vcc_backend_util.c \
	vcc_compile.c \
	vcc_dir_random.c \
	vcc_dir_round_robin.c \
	vcc_dir_dns.c \
	vcc_expr.c \
	vcc_parse.c \
	vcc_fixed_token.c \
	vcc_obj.c \
	vcc_storage.c \
	vcc_string.c \
	vcc_symb.c \
	vcc_token.c \
	vcc_var.c \
	vcc_vmod.c \
	vcc_xref.c

EXTRA_DIST = \
	generate.py

vcc_obj.c vcc_fixed_token.c vcc_token_defs.h: $(srcdir)/generate.py $(top_srcdir)/include/vrt.h
	@PYTHON@ $(top_srcdir)/lib/libvcl/generate.py $(top_srcdir) $(top_builddir)

CLEANFILES = $(builddir)/vcc_token_defs.h \
	$(builddir)/vcc_fixed_token.c \
	$(builddir)/vcc_obj.c
