#
#
RST2ANY_FLAGS = --halt=2

EXTRA_DIST = changes.rst changes.html

changes.html: changes.rst
if HAVE_RST2HTML
	${RST2HTML} ${RST2ANY_FLAGS} $? $@
else
	@echo "========================================"
	@echo "You need rst2html installed to make dist"
	@echo "========================================"
	@false
endif

# build graphviz before sphinx, so sphinx docs can use svg output
SUBDIRS = graphviz sphinx
