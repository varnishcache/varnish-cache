# $Id$

CHANGELOGS = changes-1.0.4.html changes-1.1.html

EXTRA_DIST = ${CHANGELOGS}

CLEANFILES = ${CHANGELOGS}

SUFFIXES = .xml .html

.xml.html:
	${XSLTPROC} --xinclude -o $@ $<
