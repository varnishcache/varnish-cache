# varnish.m4 - Macros to locate Varnish header files.            -*- Autoconf -*-
# serial 1 (varnish-4.0)

# Copyright (c) 2013 Varnish Software AS
# All rights reserved.
#
# Author: Tollef Fog Heen <tfheen@varnish-software.com>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#

# VARNISH_VMOD_INCLUDE_DIR([])
# ----------------------------

AC_DEFUN([VARNISH_VMOD_INCLUDES],
[
m4_pattern_forbid([^_?VARNISH[A-Z_]+$])
m4_pattern_allow([^VARNISH_VMOD(_INCLUDE_DIR|TOOL)$])
# Check for pkg-config
PKG_CHECK_EXISTS([varnishapi],[],[
	if test -n "$PKG_CONFIG"; then
		AC_MSG_FAILURE(
[The pkg-config script could not be found or is too old.  Make sure it
is in your PATH or set the PKG_CONFIG environment variable to the full
path to pkg-config.

To get pkg-config, see <http://pkg-config.freedesktop.org/>.])
	else
		AC_MSG_FAILURE(
[pkg-config was unable to locate the varnishapi configuration data.

Please check config.log or adjust the PKG_CONFIG_PATH environment
variable if you installed software in a non-standard prefix.])
	fi
])

VARNISH_PKG_GET_VAR([VMOD_INCLUDE_DIR], [vmodincludedir])
VARNISH_PKG_GET_VAR([VAPI_INCLUDE_DIR], [pkgincludedir])
_CPPFLAGS="$CPPFLAGS"
VMOD_INCLUDES="-I$VMOD_INCLUDE_DIR -I$VAPI_INCLUDE_DIR"
CPPFLAGS="$VMOD_INCLUDES $CPPFLAGS"
AC_CHECK_HEADERS([vsha256.h cache/cache.h])
CPPFLAGS="$_CPPFLAGS"
AC_SUBST([VMOD_INCLUDES])
])# VARNISH_VMOD_INCLUDE_DIR

# VARNISH_VMOD_DIR([])
# --------------------

AC_DEFUN([VARNISH_VMOD_DIR],
[
VARNISH_PKG_GET_VAR([VMOD_DIR], [vmoddir])
AC_SUBST([VMOD_DIR])
])

# VARNISH_VMODTOOL([])
# --------------------

AC_DEFUN([VARNISH_VMODTOOL],
[
VARNISH_PKG_GET_VAR([VMODTOOL], [vmodtool])
AC_SUBST([VMODTOOL])
])

# VARNISH_PKG_GET_VAR([VARIABLE, PC_VAR_NAME])
# -------------------------------

AC_DEFUN([VARNISH_PKG_GET_VAR],
[
# Uses internal function for now..
pkg_failed=no
_PKG_CONFIG([$1], [variable=][$2], [varnishapi])
if test "$pkg_failed" = "yes"; then
   AC_MSG_FAILURE([$2][ not defined, too old Varnish?])
fi
AS_VAR_COPY([$1], [pkg_cv_][$1])
])
