# varnish.m4 - Macros to locate Varnish header files.            -*- Autoconf -*-
# serial 4 (varnish-4.1.4)

# Copyright (c) 2013-2015 Varnish Software AS
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
		[vmoddir=$libdir/varnish/vmods]
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
AC_DEFUN([VARNISH_VMODS], [
	m4_foreach([_vmod_name],
		m4_split(m4_normalize([$1])),
		[_VARNISH_VMOD(_vmod_name)])
])

# VARNISH_PREREQ(VERSION)
# -----------------------
AC_DEFUN([VARNISH_PREREQ], [
	AC_REQUIRE([_VARNISH_PKG_CONFIG])
	AS_VERSION_COMPARE([$VARNISH_VERSION], [$1], [
		AC_MSG_ERROR([Varnish version $1 or higher is required.])
	])
])
