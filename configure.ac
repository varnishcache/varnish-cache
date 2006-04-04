# $Id$

AC_PREREQ(2.59)
AC_COPYRIGHT([Copyright (c) 2006 Linpro AS / Verdens Gang AS])
AC_REVISION([$Id$])
AC_INIT([Varnish], [0.1], [varnish-dev@projects.linpro.no])
AC_CONFIG_SRCDIR([include/varnishapi.h])
AC_CONFIG_HEADER([config.h])

AC_CANONICAL_SYSTEM
AC_LANG(C)

AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)

# Compiler flags (assume GCC).
# This section *must* come before AC_PROG_CC / AC_PROG_CPP.
CFLAGS="${CFLAGS:--O2}"
AC_ARG_ENABLE(wall,
	AS_HELP_STRING([--enable-wall],[use -Wall (default is NO)]),
	CFLAGS="${CFLAGS} -Wall")
AC_ARG_ENABLE(pedantic,
	AS_HELP_STRING([--enable-pedantic],[enable pedantic warnings (default is NO)]),
	CFLAGS="${CFLAGS} -pedantic")
AC_ARG_ENABLE(werror,
	AS_HELP_STRING([--enable-werror],[use -Werror (default is NO)]),
	CFLAGS="${CFLAGS} -Werror")

# Checks for programs.
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_PROG_MAKE_SET

# Checks for libraries.

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_HEADER_TIME
AC_CHECK_HEADERS([sys/socket.h])
AC_CHECK_HEADERS([netinet/in.h])
AC_CHECK_HEADERS([stddef.h])
AC_CHECK_HEADERS([stdlib.h])
AC_CHECK_HEADERS([unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_CHECK_MEMBERS([struct sockaddr.sa_len],,,[
#include <sys/types.h>
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
])

# Checks for library functions.
AC_TYPE_SIGNAL
AC_TYPE_SIZE_T
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([strerror])
AC_FUNC_STRERROR_R
AC_CHECK_FUNCS([socket])
AC_CHECK_FUNCS([strlcat])
AC_CHECK_FUNCS([strlcpy])

AC_CONFIG_FILES([
    Makefile
    bin/Makefile
    bin/varnishd/Makefile
    bin/varnishlog/Makefile
    contrib/Makefile
    include/Makefile
    lib/Makefile
    lib/libcompat/Makefile
    lib/libsbuf/Makefile
    lib/libvarnish/Makefile
    lib/libvarnishapi/Makefile
    lib/libvcl/Makefile
])
AC_CONFIG_SUBDIRS(contrib/libevent)
AC_OUTPUT
