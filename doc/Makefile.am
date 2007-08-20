# $Id$

CHANGELOGS = \
	changes-1.0.4.html \
	changes-1.1.html \
	changes-1.1.1.html

XML = \
	changes-1.0.3-1.0.4.xml \
	changes-1.0.4-1.1.xml \
	changes-1.1-1.1.1.xml \
	${CHANGELOGS:.html=.xml}

all: ${CHANGELOGS}

EXTRA_DIST = ${CHANGELOGS} ${XML} \
	changes.css changes-html.xsl

CLEANFILES = ${CHANGELOGS}

SUFFIXES = .xml .html

.xml.html:
	${XSLTPROC} --xinclude -o $@ $<

${CHANGELOGS}: changes-html.xsl
