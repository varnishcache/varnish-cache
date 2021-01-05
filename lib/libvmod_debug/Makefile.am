#

libvmod_debug_la_SOURCES = \
	vmod_debug.c \
	vmod_debug_obj.c \
	vmod_debug_dyn.c

include $(srcdir)/automake_boilerplate_debug.am

# Allow Vmod_wrong*_Data to be exported
vmod_debug_symbols_regex = 'Vmod_.*_Data'

# not --strict
vmodtoolargs_debug = --boilerplate -o vcc_debug_if

.vsc.c:
	$(PYTHON) $(top_srcdir)/lib/libvcc/vsctool.py -ch $<

VSC_SRC		= VSC_debug.vsc
VSC_GEN_C	= VSC_debug.c
VSC_GEN_H	= VSC_debug.h

CLEANFILES += $(VSC_GEN_C) $(VSC_GEN_H)

BUILT_SOURCES			 = $(VSC_GEN_C)
libvmod_debug_la_SOURCES	+= $(VSC_SRC)
