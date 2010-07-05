# $Id$

DISTCLEANFILES = vcl.7 default.vcl

if HAVE_RST2MAN
dist_man_MANS = vcl.7
endif

default.vcl:	$(top_srcdir)/bin/varnishd/default.vcl Makefile
	sed -n '/vcl_recv/,$$p' $(top_srcdir)/bin/varnishd/default.vcl > $@

vcl.7: ../doc/sphinx/reference/vcl.rst default.vcl
if HAVE_RST2MAN
	${RST2MAN} $< >$@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif
