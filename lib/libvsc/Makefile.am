#

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include

VSC_SRC = \
	VSC_lck.vsc \
	VSC_main.vsc \
	VSC_mempool.vsc \
	VSC_mgt.vsc \
	VSC_sma.vsc \
	VSC_smf.vsc \
	VSC_smu.vsc \
	VSC_vbe.vsc

VSC_GEN_C	= $(VSC_SRC:.vsc=.c)
VSC_GEN_H	= $(VSC_SRC:.vsc=.h)

$(VSC_GEN_C): vsctool.py

.vsc.c:
	$(PYTHON) $(srcdir)/vsctool.py -ch $<

noinst_LTLIBRARIES = libvsc.la
libvsc_la_SOURCES = $(VSC_SRC)

dist_pkgdata_SCRIPTS = \
	vsctool.py

BUILT_SOURCES	= $(VSC_GEN_C)
CLEANFILES	= $(VSC_GEN_C) $(VSC_GEN_H)
