# $Id$

AC_PREREQ(2.59)
AC_COPYRIGHT([Copyright (c) 2006 Linpro AS])
AC_REVISION([$Id$])
AC_INIT([Varnish-Documentation], [0.1], [varnish-dev@projects.linpro.no])
AC_CONFIG_SRCDIR([share/bibliography.xml])

AC_CANONICAL_SYSTEM

AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)

# Checks for programs.
AC_CHECK_PROGS(XMLLINT, xmllint,
    AC_MSG_ERROR([Can't proceed without xmllint]))
AC_CHECK_PROGS(XSLTPROC, xsltproc,
    AC_MSG_ERROR([Can't proceed without xsltproc]))
AC_PROG_INSTALL
AC_PROG_MAKE_SET

AC_ARG_WITH(docbook-xsl,
	AS_HELP_STRING([--with-docbook-xsl=path],[use -Wall (default is NO)]),
	AC_SUBST(DOCBOOK_XSL, "$withval"),
	AC_MSG_ERROR([Can't proceed without DocBook stylesheets]))

AC_CONFIG_FILES([
    Makefile
    en/Makefile
    en/varnish-specification/Makefile
])
AC_OUTPUT
