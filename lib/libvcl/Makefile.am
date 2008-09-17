# $Id$

INCLUDES = -I$(top_srcdir)/include

lib_LTLIBRARIES = libvcl.la

libvcl_la_LDFLAGS = -version-info 1:0:0

libvcl_la_SOURCES = \
	vcc_priv.h \
	vcc_compile.h \
	vcc_token_defs.h \
	\
	vcc_acl.c \
	vcc_action.c \
	vcc_backend.c \
	vcc_compile.c \
	vcc_dir_random.c \
	vcc_dir_round_robin.c \
	vcc_parse.c \
	vcc_fixed_token.c \
	vcc_obj.c \
	vcc_string.c \
	vcc_token.c \
	vcc_var.c \
	vcc_xref.c

EXTRA_DIST = vcc_gen_obj.tcl \
	vcc_gen_fixed_token.tcl

vcc_obj.c: $(srcdir)/vcc_gen_obj.tcl
	cd $(srcdir) && @TCLSH@ vcc_gen_obj.tcl

vcc_fixed_token.c: $(srcdir)/vcc_gen_fixed_token.tcl
	cd $(srcdir) && @TCLSH@ vcc_gen_fixed_token.tcl

vcc_token_defs.h: $(srcdir)/vcc_gen_fixed_token.tcl
	cd $(srcdir) && @TCLSH@ vcc_gen_fixed_token.tcl
