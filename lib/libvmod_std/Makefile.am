#

libvmod_std_la_SOURCES = \
	vmod_std.c \
	vmod_std_conversions.c \
	vmod_std_fileread.c \
	vmod_std_querysort.c

# Use vmodtool.py generated automake boilerplate
include $(srcdir)/automake_boilerplate_std.am

TESTS = \
	tests/std_b00001.vtc

EXTRA_DIST += $(TESTS)

vtc-refresh-tests:
	@PYTHON@ $(vmodtool) $(vmodtoolargs) $(srcdir)/vmod_std.vcc
	@cd $(top_builddir) && ./config.status --file=$(subdir)/Makefile

include $(top_srcdir)/vtc.am
