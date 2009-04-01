# $Id$

CHANGELOGS = \
	changes-2.0.4.html \
	changes-2.0.3.html \
	changes-2.0.2.html \
	changes-2.0.1.html \
	changes-2.0.html \
	changes-1.1.2.html \
	changes-1.1.1.html \
	changes-1.1.html \
	changes-1.0.4.html

XML = \
	changes-2.0.3-2.0.4.xml \
	changes-2.0.2-2.0.3.xml \
	changes-2.0.1-2.0.2.xml \
	changes-2.0-2.0.1.xml \
	changes-1.1.2-2.0.xml \
	changes-1.1.1-1.1.2.xml \
	changes-1.1-1.1.1.xml \
	changes-1.0.4-1.1.xml \
	changes-1.0.3-1.0.4.xml \
	${CHANGELOGS:.html=.xml}

all: ${CHANGELOGS}

EXTRA_DIST = ${CHANGELOGS} ${XML} \
	changes.css changes-html.xsl \
	getting-started.html

CLEANFILES = ${CHANGELOGS}

SUFFIXES = .xml .html

.xml.html:
	${XSLTPROC} --xinclude -o $@ $<

${CHANGELOGS}: changes-html.xsl
