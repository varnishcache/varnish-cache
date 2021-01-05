libvmod_cookie_la_SOURCES = \
	vmod_cookie.c

# Use vmodtool.py generated automake boilerplate
include $(srcdir)/automake_boilerplate_cookie.am

TESTS = \
	tests/cookie_b00000.vtc \
	tests/cookie_b00001.vtc \
	tests/cookie_b00002.vtc \
	tests/cookie_b00003.vtc \
	tests/cookie_b00004.vtc \
	tests/cookie_b00005.vtc \
	tests/cookie_b00006.vtc \
	tests/cookie_b00007.vtc \
	tests/cookie_b00008.vtc \
	tests/cookie_b00009.vtc \
	tests/cookie_b00010.vtc \
	tests/cookie_b00011.vtc \
	tests/cookie_b00012.vtc \
	tests/cookie_b00013.vtc \
	tests/cookie_r00028.vtc \
	tests/cookie_v00000.vtc

EXTRA_DIST += $(TESTS)

vtc-refresh-tests:
	@PYTHON@ $(vmodtool) $(vmodtoolargs) $(srcdir)/vmod_cookie.vcc
	@cd $(top_builddir) && ./config.status --file=$(subdir)/Makefile

include $(top_srcdir)/vtc.am
