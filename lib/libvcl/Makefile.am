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
	vcc_backend_util.c \
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

EXTRA_DIST = \
	generate.py

$(srcdir)/vcc_obj.c: $(srcdir)/generate.py
	cd $(srcdir) && @PYTHON3@ generate.py || true

$(srcdir)/vcc_fixed_token.c: $(srcdir)/generate.py $(top_srcdir)/include/vcl.h $(top_srcdir)/include/vrt.h $(top_srcdir)/include/vrt_obj.h
	cd $(srcdir) && @PYTHON3@ generate.py || true

$(srcdir)/vcc_token_defs.h: $(srcdir)/generate.py $(top_srcdir)/include/vcl.h $(top_srcdir)/include/vrt.h $(top_srcdir)/include/vrt_obj.h
	cd $(srcdir) && @PYTHON3@ generate.py || true
