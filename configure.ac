# $Id$

AC_PREREQ(2.59)
AC_COPYRIGHT([Copyright (c) 2006 Verdens Gang AS
Copyright (c) 2009-2009 Linpro AS])
AC_REVISION([$Id$])
AC_INIT([Varnish], [trunk], [varnish-dev@projects.linpro.no])
AC_CONFIG_SRCDIR(include/varnishapi.h)
AM_CONFIG_HEADER(config.h)

AC_CANONICAL_SYSTEM
AC_LANG(C)

AM_INIT_AUTOMAKE([foreign])

# Checks for programs.
AC_GNU_SOURCE
AC_PROG_CC
AC_PROG_CC_STDC
if test "x$ac_cv_prog_cc_c99" = xno; then
  AC_MSG_ERROR([Could not find a C99 compatible compiler])
fi
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
AC_SEARCH_LIBS(initscr, [curses ncurses],
	[have_curses=yes], [have_curses=no])
CURSES_LIBS="${LIBS}"
LIBS="${save_LIBS}"
AC_SUBST(CURSES_LIBS)
if test "$have_curses" = no; then
	AC_MSG_WARN([curses not found; some tools will not be built])
fi
AM_CONDITIONAL([HAVE_CURSES], [test x$have_curses = xyes])

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
AC_CHECK_HEADERS([sys/endian.h])
AC_CHECK_HEADERS([sys/filio.h])
AC_CHECK_HEADERS([sys/mount.h])
AC_CHECK_HEADERS([sys/socket.h])
AC_CHECK_HEADERS([sys/statvfs.h])
AC_CHECK_HEADERS([sys/vfs.h])
AC_CHECK_HEADERS([endian.h])
AC_CHECK_HEADERS([netinet/in.h])
AC_CHECK_HEADERS([pthread_np.h])
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
AC_CHECK_FUNCS([fmtcheck])
AC_CHECK_FUNCS([getdtablesize])
AC_CHECK_FUNCS([abort2])
AC_CHECK_FUNCS([timegm])
AC_CHECK_FUNCS([nanosleep])

save_LIBS="${LIBS}"
LIBS="${PTHREAD_LIBS}"
AC_CHECK_FUNCS([pthread_set_name_np])
AC_CHECK_FUNCS([pthread_mutex_isowned_np])
LIBS="${save_LIBS}"

# sendfile is tricky: there are multiple versions, and most of them
# don't work.
case $target in
*-*-freebsd*)
	AC_CACHE_CHECK([whether sendfile works],
	  [ac_cv_so_sendfile_works],
	  [AC_RUN_IFELSE(
	    [AC_LANG_PROGRAM([[
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <sys/uio.h>
	    ]],[[
		return (SF_SYNC == 0);
	    ]])],
	    [ac_cv_so_sendfile_works=yes],
	    [ac_cv_so_sendfile_works=no])
	  ])
	;;
#*-*-solaris*)
#	save_LIBS="${LIBS}"
#	LIBS="${NET_LIBS}"
#	AC_CHECK_LIB(sendfile, sendfile)
#	AC_CHECK_FUNCS([sendfile])
#	AC_CHECK_FUNCS([sendfilev])
#	NET_LIBS="${LIBS}"
#	LIBS="${save_LIBS}"
*)
	AC_MSG_WARN([won't look for sendfile() on $target])
	;;
esac
if test "$ac_cv_so_sendfile_works" = yes; then
	AC_DEFINE([SENDFILE_WORKS], [1], [Define if SENDFILE works])
fi

# Userland slab allocator, available only on Solaris
case $target in
*-*-solaris*)
	AC_CHECK_HEADERS([umem.h])
	if test "$ac_cv_have_umem_h" = yes; then
		save_LIBS="${LIBS}"
		LIBS=""
		AC_CHECK_LIB(umem, umem_alloc)
		LIBUMEM="${LIBS}"
		LIBS="${save_LIBS}"
	fi
	;;
esac
AC_SUBST(LIBUMEM)

# These functions are provided by libcompat on platforms where they
# are not available
AC_CHECK_FUNCS([asprintf vasprintf])
AC_CHECK_FUNCS([setproctitle])
AC_CHECK_FUNCS([srandomdev])
AC_CHECK_FUNCS([strlcat strlcpy])
AC_CHECK_FUNCS([strndup])
AC_CHECK_FUNCS([daemon])
AC_SYS_LARGEFILE

save_LIBS="${LIBS}"
LIBS="${LIBS} ${RT_LIBS}"
AC_CHECK_FUNCS([clock_gettime])
LIBS="${save_LIBS}"

# Check which mechanism to use for the acceptor.  We look for kqueue
# only on platforms on which we know that it works, because there are
# platforms where a simple AC_CHECK_FUNCS([kqueue]) would succeed but
# the build would fail.  We also allow the user to disable mechanisms
# he doesn't want to use.

# --enable-kqueue
AC_ARG_ENABLE(kqueue,
    AS_HELP_STRING([--enable-kqueue],
	[use kqueue if available (default is YES)]),
    ,
    [enable_kqueue=yes])

if test "$enable_kqueue" = yes; then
	case $target in
	*-*-freebsd* | *-*-darwin9* )
		AC_CHECK_FUNCS([kqueue])
		;;
	*-*-bsd*)
		# No other BSD has a sufficiently recent implementation
		AC_MSG_WARN([won't look for kqueue() on $target])
		ac_cv_func_kqueue=no
		;;
	esac
else
	ac_cv_func_kqueue=no
fi

# --enable-epoll
AC_ARG_ENABLE(epoll,
    AS_HELP_STRING([--enable-epoll],
	[use epoll if available (default is YES)]),
    ,
    [enable_epoll=yes])

if test "$enable_epoll" = yes; then
	AC_CHECK_FUNCS([epoll_ctl])
else
	ac_cv_func_epoll_ctl=no
fi

# --enable-ports
AC_ARG_ENABLE(ports,
    AS_HELP_STRING([--enable-ports],
	[use ports if available (default is YES)]),
    ,
    [enable_ports=yes])

if test "$enable_ports" = yes; then
	AC_CHECK_FUNCS([port_create])
else
	ac_cv_func_port_create=no
fi

AM_MISSING_HAS_RUN
AC_CHECK_PROGS(TCLSH, [tclsh tclsh8.4 tclsh8.5], :)
if test "$TCLSH" = :; then
  TCLSH="${am_missing_run}tclsh"
fi

# Solaris defines SO_{RCV,SND}TIMEO, but does not implement them.
# Varnish will build and run without these, but connections will not
# time out, which may leave Varnish vulnerable to denail-of-service
# attacks which would not be possible on other platforms.

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

if test "$ac_cv_so_rcvtimeo_works" = no ||
   test "$ac_cv_so_sndtimeo_works" = no; then
	AC_MSG_WARN([connection timeouts will not work])
fi

# Run-time directory
VARNISH_STATE_DIR='${localstatedir}/varnish'
AC_SUBST(VARNISH_STATE_DIR)

# Default configuration directory.
varnishconfdir='${sysconfdir}/varnish'
AC_SUBST(varnishconfdir)
# Now that we're done using the compiler to look for functions and
# libraries, set CFLAGS to what we want them to be for our own code

# This corresponds to FreeBSD's WARNS level 6
DEVELOPER_CFLAGS="-Wall -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith -Wreturn-type -Wcast-qual -Wwrite-strings -Wswitch -Wshadow -Wcast-align -Wunused-parameter -Wchar-subscripts -Winline -Wnested-externs -Wredundant-decls -Wformat"

# Additional flags for GCC 4
EXTRA_DEVELOPER_CFLAGS="-Wextra -Wno-missing-field-initializers -Wno-sign-compare"

# --enable-developer-warnings
AC_ARG_ENABLE(developer-warnings,
	AS_HELP_STRING([--enable-developer-warnings],[enable strict warnings (default is NO)]),
	CFLAGS="${CFLAGS} ${DEVELOPER_CFLAGS}")

# --enable-debugging-symbols
AC_ARG_ENABLE(debugging-symbols,
	AS_HELP_STRING([--enable-debugging-symbols],[enable debugging symbols (default is NO)]),
	CFLAGS="${CFLAGS} -O0 -g -fno-inline")

# --enable-diagnostics
AC_ARG_ENABLE(diagnostics,
	AS_HELP_STRING([--enable-diagnostics],[enable run-time diagnostics (default is NO)]),
	CFLAGS="${CFLAGS} -DDIAGNOSTICS")

# --enable-extra-developer-warnings
AC_ARG_ENABLE(extra-developer-warnings,
	AS_HELP_STRING([--enable-extra-developer-warnings],[enable even stricter warnings (default is NO)]),
	CFLAGS="${CFLAGS} ${EXTRA_DEVELOPER_CFLAGS}")

# --enable-stack-protector
AC_ARG_ENABLE(stack-protector,
	AS_HELP_STRING([--enable-stack-protector],[enable stack protector (default is NO)]),
	CFLAGS="${CFLAGS} -fstack-protector-all")

# --enable-tests
AC_ARG_ENABLE(tests,
	AS_HELP_STRING([--enable-tests],[build test programs (default is NO)]))
AM_CONDITIONAL([ENABLE_TESTS], [test x$enable_tests = xyes])

# --enable-werror
AC_ARG_ENABLE(werror,
	AS_HELP_STRING([--enable-werror],[use -Werror (default is NO)]),
	CFLAGS="${CFLAGS} -Werror")

# Command line for compiling VCL code.  I wish there were a simple way
# to figure this out dynamically without introducing a run-time
# dependency on libtool.
AC_ARG_VAR([VCC_CC], [C compiler command line for VCL code])
if test "$ac_cv_env_VCC_CC_set" = "set"; then
	VCC_CC="$ac_cv_env_VCC_CC_value"
else
	case $target in
	*-*-solaris*)
		VCC_CC="cc -Kpic -G -o %o %s"
		;;
	*-*-darwin*)
		VCC_CC="exec cc -dynamiclib -Wl,-undefined,dynamic_lookup -o %o %s"
		;;
	*)
		VCC_CC="exec cc -fpic -shared -Wl,-x -o %o %s"
		;;
	esac
fi
AC_DEFINE_UNQUOTED([VCC_CC],"$VCC_CC",[C compiler command line for VCL code])

# Use jemalloc on Linux
JEMALLOC_SUBDIR=
JEMALLOC_LDADD=
AC_ARG_ENABLE(jemalloc,
AS_HELP_STRING([--disable-jemalloc],[do not use jemalloc (default is yes on Linux, no everywhere else)]),
[if "x$enableval" = "xyes"; then
	JEMALLOC_SUBDIR=libjemalloc
	JEMALLOC_LDADD='$(top_builddir)/lib/libjemalloc/libjemalloc_mt.la'
fi],
[case $target in #(
*-*-linux*)
	JEMALLOC_SUBDIR=libjemalloc
	JEMALLOC_LDADD='$(top_builddir)/lib/libjemalloc/libjemalloc_mt.la'
	;; #(
*)
	true
	;;
esac])

AC_SUBST(JEMALLOC_SUBDIR)
AC_SUBST(JEMALLOC_LDADD)

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
    bin/varnishtest/Makefile
    bin/varnishtop/Makefile
    doc/Makefile
    etc/Makefile
    include/Makefile
    lib/Makefile
    lib/libvarnish/Makefile
    lib/libvarnishapi/Makefile
    lib/libvarnishcompat/Makefile
    lib/libvcl/Makefile
    lib/libjemalloc/Makefile
    man/Makefile
    redhat/Makefile
    varnishapi.pc
])
AC_OUTPUT
