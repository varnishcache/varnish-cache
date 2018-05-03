#

libvmod_debug_la_SOURCES = \
	vmod_debug.c \
	vmod_debug_obj.c \
	vmod_debug_dyn.c

# Use vmodtool.py generated automake boilerplate
include $(srcdir)/automake_boilerplate.am

.vsc.c:
	$(PYTHON) $(top_srcdir)/lib/libvcc/vsctool.py -ch $<

VSC_SRC		= VSC_debug.vsc
VSC_GEN_C	= VSC_debug.c
VSC_GEN_H	= VSC_debug.h

BUILT_SOURCES			 = $(VSC_GEN_C)
libvmod_debug_la_SOURCES	+= $(VSC_SRC)

$(VSC_GEN_C): $(top_srcdir)/lib/libvcc/vsctool.py

CLEANFILES += $(VSC_GEN_C) $(VSC_GEN_H)
