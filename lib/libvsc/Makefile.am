#

include $(top_srcdir)/vsc.am

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

noinst_LTLIBRARIES = libvsc.la
libvsc_la_SOURCES = $(VSC_SRC)

dist_pkgdata_SCRIPTS = vsctool.py
