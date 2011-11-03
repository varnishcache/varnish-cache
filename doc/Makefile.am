#

EXTRA_DIST = changes.rst changes.html

changes.html: changes.rst
if HAVE_RST2HTML
	${RST2HTML} $? $@
else
	@echo "========================================"
	@echo "You need rst2html installed to make dist"
	@echo "========================================"
	@false
endif

SUBDIRS = sphinx
