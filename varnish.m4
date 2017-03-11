# varnish.m4 - Macros to define VMOD builds.            -*- Autoconf -*-
# serial 5 (varnish-4.1.4)

# Copyright (c) 2016 Varnish Software AS
# All rights reserved.
#
# Author: Dridi Boukelmoune <dridi.boukelmoune@gmail.com>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above
#    copyright notice, this list of conditions and the following
#    disclaimer.
# 2. Redistributions in binary form must reproduce the above
#    copyright notice, this list of conditions and the following
#    disclaimer in the documentation and/or other materials
#    provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
# COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
# OF THE POSSIBILITY OF SUCH DAMAGE.

# _VARNISH_CHECK_EXPLICIT_BZERO()
# -------------------------------
AC_DEFUN([_VARNISH_CHECK_EXPLICIT_BZERO], [
	AC_CHECK_FUNCS([explicit_bzero])
])

# _VARNISH_PKG_CONFIG
# --------------------
AC_DEFUN([_VARNISH_PKG_CONFIG], [
	PKG_PROG_PKG_CONFIG([0.21])

	PKG_CHECK_MODULES([VARNISHAPI], [varnishapi])
	AC_SUBST([VARNISH_VERSION], [$($PKG_CONFIG --modversion varnishapi)])

	PKG_CHECK_VAR([VARNISHAPI_PREFIX], [varnishapi], [prefix])
	PKG_CHECK_VAR([VARNISHAPI_DATAROOTDIR], [varnishapi], [datarootdir])
	PKG_CHECK_VAR([VARNISHAPI_BINDIR], [varnishapi], [bindir])
	PKG_CHECK_VAR([VARNISHAPI_SBINDIR], [varnishapi], [sbindir])
	PKG_CHECK_VAR([VARNISHAPI_VMODDIR], [varnishapi], [vmoddir])

	PKG_CHECK_VAR([VMODTOOL], [varnishapi], [vmodtool])
])

# _VARNISH_CHECK_DEVEL
# --------------------
AC_DEFUN([_VARNISH_CHECK_DEVEL], [

	AC_REQUIRE([_VARNISH_PKG_CONFIG])
	AC_REQUIRE([_VARNISH_CHECK_EXPLICIT_BZERO])

	[_orig_cppflags=$CPPFLAGS]
	[CPPFLAGS=$VARNISHAPI_CFLAGS]

	AC_CHECK_HEADERS([vsha256.h cache/cache.h], [],
		[AC_MSG_ERROR([Missing Varnish development files.])])

	[CPPFLAGS=$_orig_cppflags]
])

# _VARNISH_VMOD_CONFIG
# --------------------
AC_DEFUN([_VARNISH_VMOD_CONFIG], [

	AC_REQUIRE([_VARNISH_PKG_CONFIG])
	AC_REQUIRE([_VARNISH_CHECK_DEVEL])

	dnl Check the VMOD toolchain
	AC_REQUIRE([AC_LANG_C])
	AC_REQUIRE([AC_PROG_CC_C99])
	AC_REQUIRE([AC_PROG_CPP])
	AC_REQUIRE([AC_PROG_CPP_WERROR])

	AM_PATH_PYTHON([2.6], [], [
		AC_MSG_ERROR([Python is needed to build VMODs.])
	])

	AS_IF([test -z "$RST2MAN"], [
		AC_MSG_ERROR([rst2man is needed to build VMOD manuals.])
	])

	dnl Expose the location of the std and directors VMODs
	AC_SUBST([VARNISHAPI_VMODDIR])

	dnl Expose Varnish's aclocal directory to automake
	AC_SUBST([VARNISHAPI_DATAROOTDIR])

	dnl Define the VMOD directory for libtool
	AS_CASE([$prefix],
		[NONE], [
			vmoddir=$VARNISHAPI_VMODDIR
			ac_default_prefix=$VARNISHAPI_PREFIX],
		[vmoddir=$($PKG_CONFIG --define-variable=libdir=$libdir \
			--variable=vmoddir varnishapi)]
	)
	AC_SUBST([vmoddir])

	dnl Define an automake silent execution for vmodtool
	[am__v_VMODTOOL_0='@echo "  VMODTOOL" $<;']
	[am__v_VMODTOOL_1='']
	[am__v_VMODTOOL_='$(am__v_VMODTOOL_$(AM_DEFAULT_VERBOSITY))']
	[AM_V_VMODTOOL='$(am__v_VMODTOOL_$(V))']
	AC_SUBST([am__v_VMODTOOL_0])
	AC_SUBST([am__v_VMODTOOL_1])
	AC_SUBST([am__v_VMODTOOL_])
	AC_SUBST([AM_V_VMODTOOL])

	dnl Define VMODs LDFLAGS
	AC_SUBST([VMOD_LDFLAGS],
		"-module -export-dynamic -avoid-version -shared")

	dnl Define the PATH for the test suite
	AC_SUBST([VMOD_TEST_PATH],
		[$VARNISHAPI_SBINDIR:$VARNISHAPI_BINDIR:$PATH])
])

# _VARNISH_VMOD(NAME)
# -------------------
AC_DEFUN([_VARNISH_VMOD], [

	AC_REQUIRE([_VARNISH_VMOD_CONFIG])

	VMOD_FILE="\$(abs_builddir)/.libs/libvmod_$1.so"
	AC_SUBST(m4_toupper(VMOD_$1_FILE), [$VMOD_FILE])

	VMOD_IMPORT="$1 from \\\"$VMOD_FILE\\\""
	AC_SUBST(m4_toupper(VMOD_$1), [$VMOD_IMPORT])

	VMOD_RULES="

vmod_$1.lo: vcc_$1_if.c vcc_$1_if.h

vcc_$1_if.h vmod_$1.rst vmod_$1.man.rst: vcc_$1_if.c

vcc_$1_if.c: vmod_$1.vcc
	\$(AM_V_VMODTOOL) $PYTHON $VMODTOOL -o vcc_$1_if \$(srcdir)/vmod_$1.vcc

vmod_$1.3: vmod_$1.man.rst
	$RST2MAN vmod_$1.man.rst vmod_$1.3

clean: clean-vmod-$1

distclean: clean-vmod-$1

clean-vmod-$1:
	rm -f vcc_$1_if.c vcc_$1_if.h
	rm -f vmod_$1.rst vmod_$1.man.rst vmod_$1.3

"

	AC_SUBST(m4_toupper(BUILD_VMOD_$1), [$VMOD_RULES])
	m4_ifdef([_AM_SUBST_NOTMAKE],
		[_AM_SUBST_NOTMAKE(m4_toupper(BUILD_VMOD_$1))])
])

# VARNISH_VMODS(NAMES)
# --------------------
# Since: Varnish 4.1.4
#
# Set up the VMOD tool-chain to build the collection of NAMES modules. The
# definition of key variables is made available for use in Makefile rules
# to build the modules:
#
# - VMOD_LDFLAGS (the recommended flags to link VMODs)
# - VMOD_TEST_PATH (for the test suite's environment)
# - VMODTOOL (to generate a VMOD's interface)
# - vmoddir (the install prefix for VMODs)
#
# Configuring your VMOD build with libtool can be as simple as:
#
#     AM_CFLAGS = $(VARNISHAPI_CFLAGS)
#     AM_LDFLAGS = $(VARNISHAPI_LIBS) $(VMOD_LDFLAGS)
#
#     vmod_LTLIBRARIES = libvmod_foo.la
#
#     [...]
#
# Turnkey build rules are generated for each module, they are provided as
# a convenience mechanism but offer no means of customizations. They make
# use of the VMODTOOL variable automatically.
#
# For example, if you define the following in configure.ac:
#
#     VARNISH_VMODS([foo bar])
#
# Two build rules will be available for use in Makefile.am for vmod-foo
# and vmod-bar:
#
#     vmod_LTLIBRARIES = libvmod_foo.la libvmod_bar.la
#
#     [...]
#
#     @BUILD_VMOD_FOO@
#     @BUILD_VMOD_BAR@
#
# These two set of make rules are independent and may be used in separate
# sub-directories. You still need to declare the generated VCC interfaces
# in your library's sources. The generated files can be declared this way:
#
#     nodist_libvmod_foo_la_SOURCES = \
#         vcc_foo_if.c \
#         vcc_foo_if.h
#
#     nodist_libvmod_bar_la_SOURCES = \
#         vcc_bar_if.c \
#         vcc_bar_if.h
#
# The generated rules also build the manual page, all you need to do is to
# declare the generated pages:
#
#     nodist_man_MANS = vmod_foo.3 vmod_bar.3
#
# However, it requires RST2MAN to be defined beforehand in configure.ac
# and it is for now the VMOD's maintainer job to manage it. On the other
# hand python detection is done and the resulting PYTHON variable to use
# the VMODTOOL. Since nothing requires RST2MAN to be written in python, it
# is left outside of the scope. You may even define a phony RST2MAN to
# skip man page generation as it is often the case from a dist archive.
#
# Two notable variables are exposed from Varnish's pkg-config:
#
# - VARNISHAPI_VMODDIR (locate vmod-std and vmod-directors in your tests)
# - VARNISHAPI_DATAROOTDIR (for when aclocal is called from a Makefile)
#
# For example in your root Makefile.am:
#
#     ACLOCAL_AMFLAGS = -I m4 -I ${VARNISHAPI_DATAROOTDIR}/aclocal
#
# The VARNISH_VERSION variable will be set even if the VARNISH_PREREQ macro
# wasn't called. Although many things are set up to facilitate out-of-tree
# VMOD maintenance, initialization of autoconf, automake and libtool is
# still the maintainer's responsibility. It cannot be avoided.
#
AC_DEFUN([VARNISH_VMODS], [
	m4_foreach([_vmod_name],
		m4_split(m4_normalize([$1])),
		[_VARNISH_VMOD(_vmod_name)])
])

# VARNISH_PREREQ(MINIMUM-VERSION, [MAXIMUM-VERSION])
# --------------------------------------------------
# Since: Varnish 4.1.4
#
# Verify that the version of Varnish Cache found by pkg-config is at least
# MINIMUM-VERSION. If MAXIMUM-VERSION is specified, verify that the version
# is strictly below MAXIMUM-VERSION.
#
# If the prerequisite is met, the variable VARNISH_VERSION is available.
#
AC_DEFUN([VARNISH_PREREQ], [
	AC_REQUIRE([_VARNISH_PKG_CONFIG])
	AC_REQUIRE([_VARNISH_CHECK_EXPLICIT_BZERO])
	AC_MSG_CHECKING([for Varnish])
	AC_MSG_RESULT([$VARNISH_VERSION])

	AS_VERSION_COMPARE([$VARNISH_VERSION], [$1], [
		AC_MSG_ERROR([Varnish version $1 or higher is required.])
	])

	test $# -gt 1 &&
	AS_VERSION_COMPARE([$2], [$VARNISH_VERSION], [
		AC_MSG_ERROR([Varnish version below $2 is required.])
	])
])
