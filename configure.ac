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

# asprintf() and vasprintf() are tricky, because on some systems, they
# are present in the C library, but their prototypes are hidden behind
# conditionals which we won't bother to unravel.
AC_CHECK_DECL([asprintf],
	AC_DEFINE(HAVE_ASPRINTF,1,[Define to 1 if asprintf() is available]),
	,
	[#include <stdio.h>])
AC_CHECK_DECL([vasprintf],
	AC_DEFINE(HAVE_VASPRINTF,1,[Define to 1 if vasprintf() is available]),
	,
	[#include <stdio.h>])

AC_CHECK_DECL([SO_ACCEPTFILER],
	AC_DEFINE(HAVE_ACCEPT_FILTERS,1,[Define to 1 if you have accept filters]),
	,
	[sys/types.h, sys/socket.h])

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
