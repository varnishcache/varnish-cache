#

CHANGELOGS = \
	changes-2.1.3.html \
	changes-2.1.2.html \
	changes-2.1.1.html \
	changes-2.1.0.html \
	changes-2.0.6.html \
	changes-2.0.5.html \
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
	changes-2.1.2-2.1.3.xml \
	changes-2.1.1-2.1.2.xml \
	changes-2.1.0-2.1.1.xml \
	changes-2.0.6-2.1.0.xml \
	changes-2.0.5-2.0.6.xml \
	changes-2.0.4-2.0.5.xml \
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

if HAVE_XSLTPROC
all: ${CHANGELOGS}
endif

EXTRA_DIST = ${CHANGELOGS} ${XML} \
	changes.css changes-html.xsl

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

SUBDIRS = sphinx
