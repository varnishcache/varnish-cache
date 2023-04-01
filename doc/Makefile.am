#
#
RST2ANY_FLAGS = --strict

EXTRA_DIST = changes.rst changes.html

changes.html: changes.rst
	${RST2HTML} ${RST2ANY_FLAGS} $? $@

# build graphviz before sphinx, so sphinx docs can use svg output
SUBDIRS = graphviz sphinx
