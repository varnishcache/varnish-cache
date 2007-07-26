# $Id$

CHANGELOGS = \
	changes-1.0.4.html \
	changes-1.1.html \
	changes-1.1.1.html

all: ${CHANGELOGS}

EXTRA_DIST = ${CHANGELOGS}

CLEANFILES = ${CHANGELOGS}

SUFFIXES = .xml .html

.xml.html:
	${XSLTPROC} --xinclude -o $@ $<
