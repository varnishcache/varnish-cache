# $Id$

AC_PREREQ(2.59)
AC_COPYRIGHT([Copyright (c) 2006-2007 Linpro AS / Verdens Gang AS])
AC_REVISION([$Id$])
AC_INIT([Varnish], [1.2], [varnish-dev@projects.linpro.no])
AC_CONFIG_SRCDIR(include/varnishapi.h)
AM_CONFIG_HEADER(config.h)

AC_CANONICAL_SYSTEM
AC_LANG(C)

AM_INIT_AUTOMAKE

# Checks for programs.
AC_GNU_SOURCE
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_PROG_MAKE_SET
AC_CHECK_PROGS(XSLTPROC, [xsltproc])

# Checks for libraries.
save_LIBS="${LIBS}"
LIBS=""
AC_CHECK_LIB(rt, clock_gettime)
RT_LIBS="${LIBS}"
LIBS="${save_LIBS}"
AC_SUBST(RT_LIBS)

save_LIBS="${LIBS}"
LIBS=""
AC_CHECK_LIB(dl, dlopen)
DL_LIBS="${LIBS}"
LIBS="${save_LIBS}"
AC_SUBST(DL_LIBS)

save_LIBS="${LIBS}"
LIBS=""
AC_SEARCH_LIBS(initscr, [curses ncurses])
CURSES_LIBS="${LIBS}"
LIBS="${save_LIBS}"
AC_SUBST(CURSES_LIBS)

save_LIBS="${LIBS}"
LIBS=""
AC_SEARCH_LIBS(pthread_create, [thr pthread c_r])
PTHREAD_LIBS="${LIBS}"
LIBS="${save_LIBS}"
AC_SUBST(PTHREAD_LIBS)

save_LIBS="${LIBS}"
LIBS=""
AC_CHECK_LIB(socket, socket)
AC_CHECK_LIB(nsl, getaddrinfo)
NET_LIBS="${LIBS}"
LIBS="${save_LIBS}"
AC_SUBST(NET_LIBS)

AC_CHECK_LIBM
AC_SUBST(LIBM)

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_HEADER_TIME
AC_CHECK_HEADERS([sys/socket.h])
AC_CHECK_HEADERS([sys/mount.h])
AC_CHECK_HEADERS([sys/statvfs.h])
AC_CHECK_HEADERS([sys/vfs.h])
AC_CHECK_HEADERS([netinet/in.h])
AC_CHECK_HEADERS([stddef.h])
AC_CHECK_HEADERS([stdlib.h])
AC_CHECK_HEADERS([unistd.h])
AC_CHECK_HEADERS([vis.h])

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
AC_CHECK_FUNCS([sendfile])
AC_CHECK_FUNCS([fmtcheck])

# These functions are provided by libcompat on platforms where they
# are not available
AC_CHECK_FUNCS([asprintf vasprintf])
AC_CHECK_FUNCS([setproctitle])
AC_CHECK_FUNCS([srandomdev])
AC_CHECK_FUNCS([strlcat strlcpy])
AC_CHECK_FUNCS([strndup])
AC_CHECK_FUNCS([vis strvis strvisx])
AC_CHECK_FUNCS([daemon])

save_LIBS="${LIBS}"
LIBS="${LIBS} ${RT_LIBS}"
AC_CHECK_FUNCS([clock_gettime])
LIBS="${save_LIBS}"

# Check which mechanism to use for the acceptor
AC_CHECK_FUNCS([kqueue])
AC_CHECK_FUNCS([epoll_ctl])
AC_CHECK_FUNCS([poll])

# Solaris defines SO_{RCV,SND}TIMEO, but does not implement them
AC_CACHE_CHECK([whether SO_RCVTIMEO works],
  [ac_cv_so_rcvtimeo_works],
  [AC_RUN_IFELSE(
    [AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
    ]],[[
int sd = socket(AF_INET, SOCK_STREAM, 0);
struct timeval tv = { 1, 0 };
return !!setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ]])],
    [ac_cv_so_rcvtimeo_works=yes],
    [ac_cv_so_rcvtimeo_works=no])
  ])
if test "$ac_cv_so_rcvtimeo_works" = yes; then
   AC_DEFINE([SO_RCVTIMEO_WORKS], [1], [Define if SO_RCVTIMEO works])
fi

AC_CACHE_CHECK([whether SO_SNDTIMEO works],
  [ac_cv_so_sndtimeo_works],
  [AC_RUN_IFELSE(
    [AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
    ]],[[
int sd = socket(AF_INET, SOCK_STREAM, 0);
struct timeval tv = { 1, 0 };
return !!setsockopt(sd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    ]])],
    [ac_cv_so_sndtimeo_works=yes],
    [ac_cv_so_sndtimeo_works=no])
  ])
if test "$ac_cv_so_sndtimeo_works" = yes; then
   AC_DEFINE([SO_SNDTIMEO_WORKS], [1], [Define if SO_SNDTIMEO works])
fi

# Run-time directory
VARNISH_STATE_DIR='${localstatedir}/varnish'
AC_SUBST(VARNISH_STATE_DIR)

# Now that we're done using the compiler to look for functions and
# libraries, set CFLAGS to what we want them to be for our own code

# This corresponds to FreeBSD's WARNS level 6
DEVELOPER_CFLAGS="-Wall -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith -Wreturn-type -Wcast-qual -Wwrite-strings -Wswitch -Wshadow -Wcast-align -Wunused-parameter -Wchar-subscripts -Winline -Wnested-externs -Wredundant-decls -Wformat"

# Additional flags for GCC 4
EXTRA_DEVELOPER_CFLAGS="-Wextra -Wno-missing-field-initializers -Wno-sign-compare"

AC_ARG_ENABLE(developer-warnings,
	AS_HELP_STRING([--enable-developer-warnings],[enable strict warnings (default is NO)]),
	CFLAGS="${CFLAGS} ${DEVELOPER_CFLAGS}")
AC_ARG_ENABLE(debugging-symbols,
	AS_HELP_STRING([--enable-debugging-symbols],[enable debugging symbols (default is NO)]),
	CFLAGS="${CFLAGS} -O0 -g -fno-inline")
AC_ARG_ENABLE(diagnostics,
	AS_HELP_STRING([--enable-diagnostics],[enable run-time diagnostics (default is NO)]),
	CFLAGS="${CFLAGS} -DDIAGNOSTICS")
AC_ARG_ENABLE(extra-developer-warnings,
	AS_HELP_STRING([--enable-extra-developer-warnings],[enable even stricter warnings (default is NO)]),
	CFLAGS="${CFLAGS} ${EXTRA_DEVELOPER_CFLAGS}")
AC_ARG_ENABLE(stack-protector,
	AS_HELP_STRING([--enable-stack-protector],[enable stack protector (default is NO)]),
	CFLAGS="${CFLAGS} -fstack-protector-all")
AC_ARG_ENABLE(werror,
	AS_HELP_STRING([--enable-werror],[use -Werror (default is NO)]),
	CFLAGS="${CFLAGS} -Werror")

# Generate output
AC_CONFIG_FILES([
    Makefile
    bin/Makefile
    bin/varnishadm/Makefile
    bin/varnishd/Makefile
    bin/varnishlog/Makefile
    bin/varnishhist/Makefile
    bin/varnishncsa/Makefile
    bin/varnishreplay/Makefile
    bin/varnishstat/Makefile
    bin/varnishtop/Makefile
    doc/Makefile
    etc/Makefile
    include/Makefile
    lib/Makefile
    lib/libvarnish/Makefile
    lib/libvarnishapi/Makefile
    lib/libvarnishcompat/Makefile
    lib/libvcl/Makefile
    man/Makefile
    debian/Makefile
    redhat/Makefile
    varnishapi.pc
])
AC_OUTPUT
