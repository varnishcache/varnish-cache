#
#
RST2ANY_FLAGS = --halt=2

EXTRA_DIST = changes.rst changes.html

changes.html: changes.rst
	${RST2HTML} ${RST2ANY_FLAGS} $? $@

# build graphviz before sphinx, so sphinx docs can use svg output
SUBDIRS = graphviz sphinx
