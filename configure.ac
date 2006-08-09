# $Id$

AC_PREREQ(2.59)
AC_COPYRIGHT([Copyright (c) 2006 Linpro AS / Verdens Gang AS])
AC_REVISION([$Id$])
AC_INIT([Varnish], [trunk], [varnish-dev@projects.linpro.no])
AC_CONFIG_SRCDIR(include/varnishapi.h)
AM_CONFIG_HEADER(config.h)

AC_CANONICAL_SYSTEM
AC_LANG(C)

AM_INIT_AUTOMAKE

# Compiler flags (assume GCC).
# This section *must* come before AC_PROG_CC / AC_PROG_CPP.
CFLAGS="${CFLAGS:--O2 -pipe}"
DEVELOPER_CFLAGS="-Wall -Wno-unused-parameter -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith -Wreturn-type -Wcast-qual -Wwrite-strings -Wswitch -Wshadow -Wcast-align -Wunused-parameter -Wchar-subscripts -Winline -Wnested-externs -Wredundant-decls -Wformat"
AC_ARG_ENABLE(developer-warnings,
	AS_HELP_STRING([--enable-developer-warnings],[enable strict warnings (default is NO)]),
	CFLAGS="${CFLAGS} ${DEVELOPER_CFLAGS}")
AC_ARG_ENABLE(debugging-symbols,
	AS_HELP_STRING([--enable-debugging-symbols],[enable debugging symbols (default is NO)]),
	CFLAGS="${CFLAGS} -O -g -fno-inline")
AC_ARG_ENABLE(werror,
	AS_HELP_STRING([--enable-werror],[use -Werror (default is NO)]),
	CFLAGS="${CFLAGS} -Werror")

# Checks for programs.
AC_GNU_SOURCE
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
AC_CHECK_HEADERS([sys/vfs.h])
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
AC_CHECK_FUNCS([strptime])

# These functions are provided by libcompat on platforms where they
# are not available
AC_CHECK_FUNCS([asprintf vasprintf])
AC_CHECK_FUNCS([setproctitle])
AC_CHECK_FUNCS([srandomdev])
AC_CHECK_FUNCS([strlcat strlcpy])
AC_CHECK_FUNCS([vis strvis strvisx])

# On some systems, clock_gettime is in librt rather than libc
AC_CHECK_LIB(rt, clock_gettime, need_librt=yes)
AM_CONDITIONAL(NEED_LIBRT, test x$need_librt = xyes)

# Check if dlopen() requires libdl
AC_CHECK_LIB(dl, dlopen, need_libdl=yes)
AM_CONDITIONAL(NEED_LIBDL, test x$need_libdl = xyes)

# Check for the presence of RSA's MD5 implementation (libmd on *BSD)
AC_CHECK_HEADERS([md5.h])
if test x$ac_cv_header_md5_h = xyes ; then
	AC_CHECK_LIB(md, MD5Init, need_libmd=yes)
fi
AM_CONDITIONAL(NEED_LIBMD, test x$need_libmd = xyes)

# Check which mechanism to use for the acceptor
AC_CHECK_FUNCS([kqueue])
AC_CHECK_FUNCS([epoll_ctl])
AC_CHECK_FUNCS([poll])

AC_CONFIG_FILES([
    Makefile
    bin/Makefile
    bin/varnishd/Makefile
    bin/varnishlog/Makefile
    bin/varnishncsa/Makefile
    bin/varnishstat/Makefile
    bin/varnishtop/Makefile
    include/Makefile
    lib/Makefile
    lib/libcompat/Makefile
    lib/libvarnish/Makefile
    lib/libvarnishapi/Makefile
    lib/libvcl/Makefile
])
AC_OUTPUT
