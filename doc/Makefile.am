# $Id$

CHANGELOGS = \
	changes-2.0.1.html \
	changes-2.0.html \
	changes-1.1.2.html \
	changes-1.1.1.html \
	changes-1.1.html \
	changes-1.0.4.html

XML = \
	changes-2.0-2.0.1.xml \
	changes-1.1.2-2.0.xml \
	changes-1.1.1-1.1.2.xml \
	changes-1.1-1.1.1.xml \
	changes-1.0.4-1.1.xml \
	changes-1.0.3-1.0.4.xml \
	${CHANGELOGS:.html=.xml}

if HAVE_XSLTPROC
all: ${CHANGELOGS}
endif

EXTRA_DIST = ${CHANGELOGS} ${XML} \
	changes.css changes-html.xsl \
	getting-started.html

CLEANFILES = ${CHANGELOGS}
SUFFIXES = .xml .html

.xml.html:
if HAVE_XSLTPROC
	${XSLTPROC} --xinclude -o $@ $<
else
	@echo "========================================"
	@echo "You need xsltproc installed to make dist"
	@echo "========================================"
	@false
endif

${CHANGELOGS}: changes-html.xsl

