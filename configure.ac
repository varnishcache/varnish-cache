AC_PREREQ([2.69])
AC_COPYRIGHT([Copyright (c) 2006 Verdens Gang AS
Copyright (c) 2006-2024 Varnish Software
Copyright 2010-2024 UPLEX - Nils Goroll Systemoptimierung])
AC_REVISION([$Id$])
AC_INIT([Varnish], [trunk], [varnish-dev@varnish-cache.org])
AC_CONFIG_SRCDIR(include/miniobj.h)
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AC_USE_SYSTEM_EXTENSIONS

PACKAGE_BRANCH=${PACKAGE_VERSION%.*}
AC_SUBST([PACKAGE_BRANCH])
AC_DEFINE_UNQUOTED([PACKAGE_BRANCH], ["$PACKAGE_BRANCH"],
	[Define the branch of this package.])

CFLAGS="$CFLAGS -DZ_PREFIX"

# save command line CFLAGS for use in VCC_CC (to pass through things like -m64)
# and make distcheck configure
OCFLAGS="$CFLAGS"
EXTCFLAGS="$CFLAGS"
AC_SUBST(EXTCFLAGS)

AC_CANONICAL_TARGET
AC_LANG(C)

AM_MAINTAINER_MODE([disable])
AM_INIT_AUTOMAKE([1.13 foreign color-tests parallel-tests subdir-objects])
AM_EXTRA_RECURSIVE_TARGETS([recheck])
AM_SILENT_RULES([yes])
AC_DISABLE_STATIC
LT_INIT

# Checks for programs.
AC_PROG_CC
# XXX remove the next line when AC_PREREQ >= 2.70 and
# replace with check of $ac_prog_cc_stdc
AC_PROG_CC_C99

AX_PTHREAD(,[AC_MSG_ERROR([Could not configure pthreads support])])

LIBS="$PTHREAD_LIBS $LIBS"
CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
CC="$PTHREAD_CC"

AC_PROG_INSTALL

AC_ARG_WITH([rst2man],
  AS_HELP_STRING([--with-rst2man=PATH], [Location of rst2man (auto)]),
  [RST2MAN="$withval"],
  [AC_CHECK_PROGS(RST2MAN,
    [rst2man-3.6 rst2man-3 rst2man rst2man.py],
    [no])])
if test "x$RST2MAN" = "xno"; then
  AC_MSG_ERROR(
    [rst2man is needed to build Varnish, please install python3-docutils.])
fi

AC_ARG_WITH([sphinx-build],
  AS_HELP_STRING([--with-sphinx-build=PATH], [Location of sphinx-build (auto)]),
  [SPHINX="$withval"],
  [AC_CHECK_PROGS(SPHINX,
    [sphinx-build-3.6 sphinx-build-3 sphinx-build],
    [no])])
if test "x$SPHINX" = "xno"; then
  AC_MSG_ERROR(
    [sphinx-build is needed to build Varnish, please install python3-sphinx.])
fi

AC_ARG_WITH([rst2html],
  AS_HELP_STRING([--with-rst2html=PATH], [Location of rst2html (auto)]),
  [RST2HTML="$withval"],
  [AC_CHECK_PROGS(RST2HTML,
     [rst2html-3.6 rst2html-3 rst2html rst2html.py],
     "no")])

if test "x$RST2HTML" = "xno"; then
   AC_MSG_ERROR(
     [rst2html not found - (Weird, we found rst2man?!)])
fi

AC_ARG_VAR([DOT], [The dot program from graphviz to build SVG graphics])
AM_MISSING_PROG([DOT], [dot])
AC_CHECK_PROGS([DOT], [dot])

# Define VMOD flags
_VARNISH_VMOD_LDFLAGS

# Check for python.
_VARNISH_CHECK_PYTHON

# Check for libraries.
_VARNISH_SEARCH_LIBS(pthread, pthread_create, [thr pthread c_r])
_VARNISH_CHECK_LIB(dl, dlopen)
_VARNISH_CHECK_LIB(socket, socket)
_VARNISH_CHECK_LIB(nsl, getaddrinfo)

AC_SUBST(NET_LIBS, "${SOCKET_LIBS} ${NSL_LIBS}")

# Userland slab allocator from Solaris, ported to other systems
AC_CHECK_HEADERS([umem.h])

# More portable vmb.h
AC_CHECK_HEADERS([stdatomic.h])

# XXX: This _may_ be for OS/X
LT_LIB_M
AC_SUBST(LIBM)

m4_ifndef([PKG_PROG_PKG_CONFIG], [m4_fatal([pkg.m4 missing, please install pkg-config])])
PKG_PROG_PKG_CONFIG
if test -n $PKG_CONFIG; then
   PKG_CHECK_MODULES([PCRE2], [libpcre2-8])
else
   AC_CHECK_PROG(PCRE2_CONFIG, pcre2-config, pcre2-config)
   AC_ARG_WITH(pcre2-config,
               AS_HELP_STRING([--with-pcre2-config=PATH],
                              [Location of PCRE2 pcre2-config (auto)]),
               [pcre2_config="$withval"],
               [pcre2_config=""])

  if test "x$pcre2_config" != "x" ; then
    AC_MSG_CHECKING(for $pcre2_config)

    if test -f $pcre2_config ; then
      PCRE2_CONFIG=$pcre2_config
      AC_MSG_RESULT(yes)
    else
      AC_MSG_RESULT(no - searching PATH)
    fi
  fi
  if test "x$PCRE2_CONFIG" = "x"; then
    AC_CHECK_PROGS(PCRE2_CONFIG, pcre2-config)
  fi
  PCRE2_CFLAGS=`$PCRE2_CONFIG --cflags`
  PCRE2_LIBS=`$PCRE2_CONFIG --libs8`
fi
AC_SUBST(PCRE2_CFLAGS)
AC_SUBST(PCRE2_LIBS)

save_LIBS="${LIBS}"
LIBS="${LIBS} ${PCRE2_LIBS}"
AC_CHECK_FUNCS([pcre2_set_depth_limit_8], [
  AC_DEFINE([HAVE_PCRE2_SET_DEPTH_LIMIT], [1], [Use pcre2_set_depth_limit()])
])
LIBS="${save_LIBS}"

# --enable-pcre2-jit
AC_ARG_ENABLE(pcre2-jit,
  AS_HELP_STRING([--enable-pcre2-jit],
    [use the PCRE2 JIT compiler (default is YES)]),
    [],
    [enable_pcre2_jit=yes])
if test "$enable_pcre2_jit" = yes; then
  AC_DEFINE([USE_PCRE2_JIT], [1], [Use the PCRE2 JIT compiler])
fi


AC_CHECK_HEADERS([edit/readline/readline.h],
	[AC_DEFINE([HAVE_LIBEDIT], [1], [Define if we have libedit])
	LIBEDIT_LIBS="-ledit"],

	[PKG_CHECK_MODULES([LIBEDIT], [libedit],
		[
		# having the module does not imply having the header
		AC_SUBST(LIBEDIT_CFLAGS)
		AC_SUBST(LIBEDIT_LIBS)
		save_CFLAGS="${CFLAGS}"
		CFLAGS="${CFLAGS} ${LIBEDIT_CFLAGS}"
		AC_CHECK_HEADERS([editline/readline.h],
				  [AC_DEFINE([HAVE_LIBEDIT], [1], [Define if we have libedit])],
				  [AC_MSG_ERROR([Found libedit, but header file is missing. Hint: Install dev package?])])
		CFLAGS="${save_CFLAGS}"
		],
		[
		 # AX_LIB_READLINE overwrites LIBS which leads to every binary getting
		 # linked against libreadline uselessly. So we re-use LIBEDIT_LIBS which
		 # we have for libedit to add the lib specifically where needed
		 save_LIBS="${LIBS}"
		 AX_LIB_READLINE
		 LIBS="${save_LIBS}"
		 if test "$ax_cv_lib_readline" = "no"; then
			AC_MSG_ERROR([neither libedit nor another readline compatible library found])
		 fi
		 if test "x$ax_cv_lib_readline_history" != "xyes"; then
			AC_MSG_ERROR([need readline history support])
		 fi
		 LIBEDIT_LIBS="$ax_cv_lib_readline"
		])
	])

PKG_CHECK_MODULES([CURSES], [ncursesw], [], [
	PKG_CHECK_MODULES([CURSES], [ncurses], [], [
		PKG_CHECK_MODULES([CURSES], [curses], [], [
AX_WITH_CURSES
if test "x$ax_cv_curses" != xyes; then
	AC_MSG_ERROR([requires an X/Open-compatible Curses library])
fi
CURSES_LIBS="$CURSES_LIB"
		])
	])
])
AC_SUBST([CURSES_CFLAGS])
AC_SUBST([CURSES_LIBS])

save_CFLAGS="${CFLAGS}"
CFLAGS="${CFLAGS} ${CURSES_CFLAGS}"
AC_CHECK_HEADERS([ncursesw/curses.h ncursesw.h ncurses/curses.h ncurses.h curses.h])
CFLAGS="${save_CFLAGS}"

# Checks for header files.
AC_CHECK_HEADERS([sys/filio.h])
AC_CHECK_HEADERS([sys/personality.h])
AC_CHECK_HEADERS([pthread_np.h], [], [], [#include <pthread.h>])
AC_CHECK_HEADERS([priv.h])
AC_CHECK_HEADERS([fnmatch.h], [], [AC_MSG_ERROR([fnmatch.h is required])])

# Checks for library functions.
AC_CHECK_FUNCS([setppriv])
AC_CHECK_FUNCS([fallocate])
AC_CHECK_FUNCS([closefrom])
AC_CHECK_FUNCS([getpeereid])
AC_CHECK_FUNCS([getpeerucred])
AC_CHECK_FUNCS([fnmatch], [], [AC_MSG_ERROR([fnmatch(3) is required])])

save_LIBS="${LIBS}"
LIBS="${PTHREAD_LIBS}"
AC_CHECK_FUNCS([pthread_mutex_isowned_np])
AC_CHECK_FUNCS([pthread_getattr_np])
LIBS="${save_LIBS}"

AC_CHECK_DECL([__SUNPRO_C], [SUNCC="yes"], [SUNCC="no"])

AC_ARG_ENABLE(ubsan,
	AS_HELP_STRING([--enable-ubsan],
		[enable undefined behavior sanitizer (default is NO)]),
	[
		AC_DEFINE([ENABLE_UBSAN], [1],
			[Define to 1 if UBSAN is enabled.])
		UBSAN_FLAGS=-fsanitize=undefined
	])

AC_ARG_ENABLE(tsan,
	AS_HELP_STRING([--enable-tsan],
		[enable thread sanitizer (default is NO)]),
	[
		AC_DEFINE([ENABLE_TSAN], [1],
			[Define to 1 if TSAN is enabled.])
		TSAN_FLAGS=-fsanitize=thread
	])

AC_ARG_ENABLE(asan,
	AS_HELP_STRING([--enable-asan],
		[enable address sanitizer (default is NO)]),
	[
		AC_DEFINE([ENABLE_ASAN], [1],
			[Define to 1 if ASAN sanitizer is enabled.])
		ASAN_FLAGS=-fsanitize=address
	])

if test -n "$ASAN_FLAGS"; then
	AX_CHECK_COMPILE_FLAG(
		[$ASAN_FLAGS -fsanitize-address-use-after-scope],
		[ASAN_FLAGS="$ASAN_FLAGS -fsanitize-address-use-after-scope"])
fi

AC_ARG_ENABLE(msan,
	AS_HELP_STRING([--enable-msan],
		[enable memory sanitizer (default is NO)]),
	[
		AC_DEFINE([ENABLE_MSAN], [1],
			[Define to 1 if MSAN is enabled.])
		MSAN_FLAGS=-fsanitize=memory
	])

if test "x$UBSAN_FLAGS$TSAN_FLAGS$ASAN_FLAGS$MSAN_FLAGS" != "x"; then
	AC_DEFINE([ENABLE_SANITIZER], [1],
		[Define to 1 if any sanitizer is enabled.])
	SAN_FLAGS="$ASAN_FLAGS $UBSAN_FLAGS $TSAN_FLAGS $MSAN_FLAGS"
	SAN_CFLAGS="$SAN_FLAGS -fPIC -fPIE -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer"
	SAN_LDFLAGS=
	save_CFLAGS=$CFLAGS
	CFLAGS="${CFLAGS} -Werror=unused-command-line-argument"
	AX_CHECK_LINK_FLAG([-pie], [SAN_LDFLAGS=-pie])
	CFLAGS=$save_CFLAGS
	case $CC in
	gcc*)
		SAN_CFLAGS="$SAN_CFLAGS -fuse-ld=gold"
		;;
	esac
	CFLAGS="$CFLAGS $SAN_CFLAGS"
	LDFLAGS="$LDFLAGS $SAN_LDFLAGS"
fi

AC_ARG_ENABLE(workspace-emulator,
	AS_HELP_STRING([--enable-workspace-emulator],
		[emulate workspace allocations (default is NO)]),
    [], [enable_workspace_emulator=no])

AM_CONDITIONAL([ENABLE_WORKSPACE_EMULATOR],
	[test "$enable_workspace_emulator" = yes])

AM_COND_IF([ENABLE_WORKSPACE_EMULATOR], [
	AC_CHECK_HEADERS([sanitizer/asan_interface.h])
	AC_DEFINE([ENABLE_WORKSPACE_EMULATOR], [1],
              [Define to 1 if the workspace emulator is enabled])
])

# Use jemalloc on Linux
JEMALLOC_LDADD=
AC_ARG_WITH([jemalloc],
            [AS_HELP_STRING([--with-jemalloc],
              [use jemalloc memory allocator.  Default is yes on Linux,  no elsewhere])],
            [],
            [with_jemalloc=check])

case $target in
    *-*-linux*)
        if test "x$with_jemalloc" != xno; then
            AC_CHECK_LIB([jemalloc], [malloc_conf],
                  [JEMALLOC_LDADD="-ljemalloc"],
                  [AC_MSG_WARN([No system jemalloc found, using system malloc])])
	fi
	;;
esac
AC_SUBST(JEMALLOC_LDADD)

AC_CHECK_FUNCS([setproctitle])

AC_SEARCH_LIBS(backtrace, [execinfo], [AC_CHECK_HEADERS([[execinfo.h]])])

# if the default libexecinfo on alpine causes issues, you can use libunwind
AC_ARG_WITH([unwind],
            [AS_HELP_STRING([--with-unwind],
              [use libunwind to print stacktraces (use libexecinfo otherwise). Recommended on alpine linux. Defaults to auto.])])

if test "$with_unwind" != no; then
    case $target in
    *-*-darwin*)
        # Always present but .pc is not installed
        have_unwind=yes
        ;;
    *)
        PKG_CHECK_MODULES([LIBUNWIND], [libunwind], [have_unwind=yes],
                [have_unwind=no])
        ;;
    esac
fi

if test "$with_unwind" = yes && test "$have_unwind" != yes; then
        AC_MSG_ERROR([Could not find libunwind])
fi

if test "$have_unwind" = yes; then
    AC_DEFINE([WITH_UNWIND], [1],
              [Define to 1 to use libunwind instead of libexecinfo])
elif test "$ac_cv_search_backtrace" = no; then
	AC_MSG_ERROR([Could not find backtrace() support])
elif test "$ac_cv_header_execinfo_h" = no; then
	AC_MSG_ERROR([Could not find execinfo.h])
fi

AM_CONDITIONAL([WITH_UNWIND], [test "$have_unwind" = yes])

case $target in
*-*-darwin*)
	# white lie - we don't actually test it
	# present but not functional
	AC_MSG_CHECKING([whether daemon() works])
	AC_MSG_RESULT([no])
	ac_cv_func_daemon=no
	;;
*)
	AC_CHECK_FUNCS([daemon])
	;;
esac
AM_CONDITIONAL(HAVE_DAEMON, [test "x$ac_cv_func_daemon" != "xno"])

AC_SYS_LARGEFILE

save_LIBS="${LIBS}"
LIBS="${LIBS} ${RT_LIBS}"
AC_CHECK_FUNCS([clock_gettime])
AC_CHECK_FUNCS([gethrtime])
LIBS="${save_LIBS}"

if test "x$ac_cv_func_gethrtime" = xyes && \
   test "x$ac_cv_func_clock_gettime" = xyes ; then
  AC_MSG_CHECKING(if clock_gettime is faster than gethrtime)
  AC_RUN_IFELSE(
    [AC_LANG_PROGRAM([[
#include <sys/time.h>
#include <time.h>
#include <stdio.h>

static hrtime_t cl()
{
	struct timespec ts;

	(void) clock_gettime(CLOCK_MONOTONIC, &ts);
	return (ts.tv_sec * 1e9 + ts.tv_nsec);
}
    ]],[[
	hrtime_t s, c, e, t_hr, t_cl;
	int i, r, wins;

	wins = 0;
	for (r = 0; r < 10; r++) {
		c = 0;
		s = gethrtime();
		for (i=0; i<100000; i++)
			c += gethrtime();
		e = gethrtime();
		t_hr = e - s;
		fprintf(stderr, "hrtime\t\t%12lu check %lu\n",
		    (unsigned long)t_hr, (unsigned long)c);

		c = 0;
		s = gethrtime();
		for (i=0; i<100000; i++)
			c += cl();
		e = gethrtime();
		t_cl = e - s;
		fprintf(stderr, "clock_gettime\t%12lu check %lu\n",
		    (unsigned long)t_cl, (unsigned long)c);

		if (t_cl * 2 < t_hr)
			wins++;
	}
	fprintf(stderr, "clock_gettime wins %d/%d\n", wins, r);
	if (2 * wins >= r)
		return (0);
	return (1);
    ]])],
    [AC_MSG_RESULT(yes)
    ],
    [AC_MSG_RESULT(no)
     AC_DEFINE([USE_GETHRTIME], [1], [Define if gethrtime is preferred])
    ]
  )
fi

# --enable-kqueue
AC_ARG_ENABLE(kqueue,
    AS_HELP_STRING([--enable-kqueue],
	[use kqueue if available (default is YES)]),
    ,
    [enable_kqueue=yes])

if test "$enable_kqueue" = yes; then
	AC_CHECK_FUNCS([kqueue])
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

# --with-persistent-storage
AC_ARG_WITH(persistent-storage,
    AS_HELP_STRING([--with-persistent-storage],
	[use deprecated persistent storage (default is NO)]),
    [],
    [with_persistent_storage=no])

if test "$with_persistent_storage" = yes; then
	AC_DEFINE([WITH_PERSISTENT_STORAGE], [1],
		[Define to 1 to build the deprecated persistent storage.])
fi

AM_CONDITIONAL([WITH_PERSISTENT_STORAGE],
	[test "$with_persistent_storage" = yes])

AM_MISSING_HAS_RUN

AC_CHECK_MEMBER([struct sockaddr.sa_len],
	[AC_DEFINE([HAVE_STRUCT_SOCKADDR_SA_LEN], [1],
		[Define if sa_len is present in struct sockaddr])],
	[], [#include <sys/socket.h>])

AC_CHECK_DECL([SO_ACCEPTFILTER],
    AC_DEFINE(HAVE_ACCEPT_FILTERS,1,[Define to 1 if you have accept filters]),
    [], [
        #include <sys/types.h>
        #include <sys/socket.h>
    ])

AC_CHECK_DECL([SO_RCVTIMEO],
    [],
    AC_MSG_ERROR([SO_RCVTIMEO is needed to build Varnish.]), [
        #include <sys/types.h>
        #include <sys/socket.h>
    ])

AC_CHECK_DECL([SO_SNDTIMEO],
    [],
    AC_MSG_ERROR([SO_SNDTIMEO is needed to build Varnish.]), [
        #include <sys/types.h>
        #include <sys/socket.h>
    ])

# Check if the OS supports TCP_KEEP(CNT|IDLE|INTVL) socket options
save_LIBS="${LIBS}"
LIBS="${LIBS} ${NET_LIBS}"
AC_CACHE_CHECK([for TCP_KEEP(CNT|IDLE|INTVL) socket options],
  [ac_cv_have_tcp_keep],
  [AC_RUN_IFELSE(
    [AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
    ]],[[
int s = socket(AF_INET, SOCK_STREAM, 0);
int i = 5;
if (s < 0 && errno == EPROTONOSUPPORT)
  s = socket(AF_INET6, SOCK_STREAM, 0);
if (setsockopt(s, IPPROTO_TCP, TCP_KEEPCNT, &i, sizeof i))
  return (1);
if (setsockopt(s, IPPROTO_TCP, TCP_KEEPIDLE, &i, sizeof i))
  return (1);
if (setsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL, &i, sizeof i))
  return (1);
return (0);
    ]])],
    [ac_cv_have_tcp_keep=yes],
    [ac_cv_have_tcp_keep=no])
  ])
if test "$ac_cv_have_tcp_keep" = yes; then
   AC_DEFINE([HAVE_TCP_KEEP], [1], [Define if OS supports TCP_KEEP* socket options])
else
   # Check TCP_KEEPALIVE on macOs which uses seconds as idle time unit like TCP_KEEPIDLE
   AC_CACHE_CHECK([for TCP_KEEPALIVE socket option],
     [ac_cv_have_tcp_keepalive],
     [AC_RUN_IFELSE(
     [AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
    ]],[[
int s = socket(AF_INET, SOCK_STREAM, 0);
int i = 5;
if (s < 0)
  return (1);
if (setsockopt(s, IPPROTO_TCP, TCP_KEEPALIVE, &i, sizeof i))
  return (1);
return 0;
    ]])],
    [ac_cv_have_tcp_keepalive=yes],
    [ac_cv_have_tcp_keepalive=no])
  ])
  if test "$ac_cv_have_tcp_keepalive" = yes; then
     AC_DEFINE([HAVE_TCP_KEEPALIVE], [1], [Define if OS supports TCP_KEEPALIVE socket option])
  fi
fi
LIBS="${save_LIBS}"

# Check if the OS supports TCP_FASTOPEN socket option
save_LIBS="${LIBS}"
LIBS="${LIBS} ${NET_LIBS}"
AC_CACHE_CHECK([for TCP_FASTOPEN socket option],
  [ac_cv_have_tcp_fastopen],
  [AC_RUN_IFELSE(
    [AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>

#ifndef SOL_TCP
#  define SOL_TCP IPPROTO_TCP
#endif
    ]],[[
int s = socket(AF_INET, SOCK_STREAM, 0);
int i = 5;
if (s < 0 && errno == EPROTONOSUPPORT)
  s = socket(AF_INET6, SOCK_STREAM, 0);
if (setsockopt(s, SOL_TCP, TCP_FASTOPEN, &i, sizeof i))
  return (1);
return (0);
    ]])],
    [ac_cv_have_tcp_fastopen=yes],
    [ac_cv_have_tcp_fastopen=no])
  ])
if test "$ac_cv_have_tcp_fastopen" = yes; then
   AC_DEFINE([HAVE_TCP_FASTOPEN], [1], [Define if OS supports TCP_FASTOPEN socket option])
fi
LIBS="${save_LIBS}"

AC_CHECK_FUNCS([close_range])

# Check for working close_range()
if test "$ac_cv_func_close_range" = yes; then
AC_CACHE_CHECK([if close_range is working],
  [ac_cv_have_working_close_range],
  [AC_RUN_IFELSE(
    [AC_LANG_PROGRAM([[
#include <unistd.h>
    ]],[[
return (close_range(0, 2, 0));
    ]])],
    [ac_cv_have_working_close_range=yes],
    [ac_cv_have_working_close_range=no])
  ])
fi
if test "x$ac_cv_have_working_close_range" = xyes; then
   AC_DEFINE([HAVE_WORKING_CLOSE_RANGE], [1],
	     [Define if OS has working close_range()])
fi

# Run-time directory
if test "${localstatedir}" = '${prefix}/var' ; then
   VARNISH_STATE_DIR='/var/run'
else
   VARNISH_STATE_DIR='${localstatedir}/varnish'
fi
AC_SUBST(VARNISH_STATE_DIR)

# Default configuration directory.
pkgsysconfdir='${sysconfdir}/varnish'
AC_SUBST(pkgsysconfdir)

# VMOD variables
AC_SUBST(vmoddir, [$\(pkglibdir\)/vmods])
AC_SUBST(VMODTOOL, [$\(top_srcdir\)/lib/libvcc/vmodtool.py])
AC_SUBST(VSCTOOL, [$\(top_srcdir\)/lib/libvsc/vsctool.py])

# Check for linker script support
gl_LD_VERSION_SCRIPT

#######################################################################
# Now that we're done using the compiler to look for functions and
# libraries, set CFLAGS to what we want them to be for our own code

# This is a test to see how much havoc Jenkins exposes.
#
# The reason for -Wno-error=unused-result is a glibc/gcc interaction
# idiocy where write is marked as warn_unused_result, causing build
# failures.

WFLAGS=

AX_CHECK_COMPILE_FLAG([-Wall],
     [CFLAGS="${CFLAGS} -Wall"
      WFLAGS="${WFLAGS} -Wall"])

if test "$SUNCC" = "yes" ; then
    SUNCC_CFLAGS=" \
	-errwarn=%all,no%E_EMPTY_TRANSLATION_UNIT \
	-errtags=yes \
	"
    AX_CHECK_COMPILE_FLAG([${SUNCC_CFLAGS}],
	[CFLAGS="${CFLAGS} ${SUNCC_CFLAGS}"
	 WFLAGS="${WFLAGS} ${SUNCC_CFLAGS}"])
else
    AX_CHECK_COMPILE_FLAG([-Werror],
	[CFLAGS="${CFLAGS} -Werror"
	 WFLAGS="${WFLAGS} -Werror"])
fi

case $target in
    *-*-darwin*)
	AX_CHECK_COMPILE_FLAG([-Wno-expansion-to-defined],
	[CFLAGS="${CFLAGS} -Wno-expansion-to-defined"
	 WFLAGS="${WFLAGS} -Wno-expansion-to-defined"])
	;;
esac

AX_CHECK_COMPILE_FLAG([-Werror=unused-result],
    [CFLAGS="${CFLAGS} -Wno-error=unused-result"
     WFLAGS="${WFLAGS} -Wno-error=unused-result"],
    [AX_CHECK_COMPILE_FLAG([-Wunused-result],
     [CFLAGS="${CFLAGS} -Wno-unused-result"
      WFLAGS="${WFLAGS} -Wno-unused-result"])])

# This corresponds to FreeBSD's WARNS level 6
DEVELOPER_CFLAGS=`$PYTHON $srcdir/wflags.py`
if test $? -ne 0 ; then
  AC_MSG_ERROR([wflags.py failure])
fi

# zlib-specific flags
AC_SUBST(VGZ_CFLAGS)

# Support for visibility attribute (zlib)
AC_CACHE_CHECK([whether we have support for visibility attributes],
	[ac_cv_have_viz],
	[AC_COMPILE_IFELSE(
		[AC_LANG_PROGRAM([[
			int __attribute__((visibility ("hidden"))) foo;
		]],[])],
		[ac_cv_have_viz=yes],
		[ac_cv_have_viz=no])
])

AS_IF([test $ac_cv_have_viz = yes], [
	AC_DEFINE([HAVE_HIDDEN], [1],
		[Define to 1 if visibility attribute hidden is available.])])

# --enable-stack-protector
AC_ARG_ENABLE(stack-protector,
       AS_HELP_STRING([--enable-stack-protector],[enable stack protector (default is YES)]),
       [],
       [enable_stack_protector=yes])

if test "x$enable_stack_protector" != "xno"; then
	AX_CHECK_COMPILE_FLAG([-fstack-protector],
		AX_CHECK_LINK_FLAG([-fstack-protector],
			[DEVELOPER_CFLAGS="${DEVELOPER_CFLAGS} -fstack-protector"], [], []),
		[], [])
fi

# --enable-developer-warnings
AC_ARG_ENABLE(developer-warnings,
	AS_HELP_STRING([--enable-developer-warnings],[enable strict warnings (default is NO)]),
	[],
	[enable_developer_warnings=no])

if test "x$SUNCC" != "xyes" && test "x$enable_developer_warnings" != "xno"; then
	# no known way to specifically disabling missing-field-initializers
	# warnings keeping the rest of -Wextra
	AX_CHECK_COMPILE_FLAG([-Wno-missing-field-initializers],
		[DEVELOPER_CFLAGS="${DEVELOPER_CFLAGS} -Wno-missing-field-initializers"],
		[DEVELOPER_CFLAGS="${DEVELOPER_CFLAGS} -Wno-extra"],
		[])

	CFLAGS="${CFLAGS} ${DEVELOPER_CFLAGS}"
	WFLAGS="${WFLAGS} ${DEVELOPER_CFLAGS}"
fi

# gcc on solaris needs -fstack-protector when calling gcc in linker
# mode but libtool does not pass it on, so we need to trick it
# specifically
case $CFLAGS in
  *-fstack-protector*)
    case $target in
      *-*-solaris*)
	case $CC in
	  gcc*)
	    AM_LT_LDFLAGS="${AM_LT_LDFLAGS} -Wc,-fstack-protector"
	    ;;
	esac
	;;
    esac
    ;;
esac

AC_SUBST(AM_LT_LDFLAGS)

# --enable-coverage
AC_ARG_ENABLE(coverage,
	AS_HELP_STRING([--enable-coverage],
		[enable coverage (implies debugging symbols, default is NO)]),
	[],
	[enable_coverage=no])

# --enable-debugging-symbols
AC_ARG_ENABLE(debugging-symbols,
	AS_HELP_STRING([--enable-debugging-symbols],
		[enable debugging symbols (default is NO)]),
	[],
	[enable_debugging_symbols=no])

if test "$enable_coverage" != no; then
	AC_DEFINE([ENABLE_COVERAGE], [1], [Define to 1 if code coverage is enabled.])
	save_CFLAGS=$CFLAGS
	CFLAGS=
	AX_CHECK_COMPILE_FLAG([--coverage],
		[COV_FLAGS=--coverage],
		[AX_CHECK_COMPILE_FLAG([-fprofile-arcs -ftest-coverage],
			[COV_FLAGS="-fprofile-arcs -ftest-coverage"])])
	AX_CHECK_COMPILE_FLAG([-fprofile-abs-path],
		[COV_FLAGS="$COV_FLAGS -fprofile-abs-path"])
	AX_CHECK_COMPILE_FLAG([-fPIC], [COV_FLAGS="$COV_FLAGS -fPIC"])
	CFLAGS=$COV_FLAGS
	AC_CHECK_FUNCS([__gcov_flush])
	AC_CHECK_FUNCS([__gcov_dump])
	AC_CHECK_FUNCS([__llvm_gcov_flush])
	CFLAGS="$save_CFLAGS $COV_FLAGS"
	enable_debugging_symbols=yes
fi

if test "$enable_debugging_symbols" != no; then
	if test "x$SUNCC" = "xyes" ; then
		CFLAGS="${CFLAGS} -O0 -g"
	else
		CFLAGS="${CFLAGS} -O0 -g -fno-inline"
	fi
fi

# --enable-oss-fuzz
AC_ARG_ENABLE(oss-fuzz,
	AS_HELP_STRING([--enable-oss-fuzz],
		[enable build tweaks for OSS-Fuzz (default is NO)]),
	[],
	[enable_oss_fuzz=no])

AM_CONDITIONAL(ENABLE_OSS_FUZZ, [test "$enable_oss_fuzz" != no])

# Command line for compiling VCL code.  I wish there were a simple way
# to figure this out dynamically without introducing a run-time
# dependency on libtool.
AC_ARG_VAR([VCC_CC], [C compiler command line for VCL code])
if test "$ac_cv_env_VCC_CC_set" = "set"; then
	VCC_CC="$ac_cv_env_VCC_CC_value"
else
	case $target in
	*-*-darwin*)
		VCC_CC="cc $OCFLAGS"
		;;
	*)
		VCC_CC="$PTHREAD_CC $OCFLAGS"
		;;
	esac

	save_CFLAGS="$CFLAGS"
	save_CC="$CC"
	CFLAGS=
	CC="$VCC_CC"
	AX_CHECK_COMPILE_FLAG(
		[-fno-var-tracking-assignments],
		[VCC_CC="$VCC_CC -fno-var-tracking-assignments"])
	CFLAGS="$save_CFLAGS"
	CC="$save_CC"

	case $target in
	*-*-solaris*)
		case $PTHREAD_CC in
		*gcc*)
			VCC_CC="exec $VCC_CC %w $PTHREAD_CFLAGS -fpic -shared -o %o %s"
			;;
		*cc)
			VCC_CC="exec $VCC_CC %w -errwarn=%%all,no%%E_STATEMENT_NOT_REACHED $PTHREAD_CFLAGS -Kpic -G -o %o %s"
			;;
		esac
		;;
	*-*-darwin*)
		VCC_CC="exec $VCC_CC %w -dynamiclib -Wl,-undefined,dynamic_lookup -o %o %s"
		;;
	*)
		VCC_CC="exec $VCC_CC %w $PTHREAD_CFLAGS $SAN_CFLAGS -fpic -shared -Wl,-x -o %o %s"
		;;
	esac
fi

if test "$ac_cv_env_VCC_WARN_set" = set; then
	VCC_WARN=$ac_cv_env_VCC_WARN_value
else
	VCC_WARN=$WFLAGS
fi

OCFLAGS="$OCFLAGS $WFLAGS"

AC_DEFINE_UNQUOTED([VCC_CC],"$VCC_CC",[C compiler command line for VCL code])
AC_DEFINE_UNQUOTED([VCC_WARN],"$VCC_WARN",[C compiler warnings for VCL code])

# Stupid automake needs this
VTC_TESTS="$(cd $srcdir/bin/varnishtest && echo tests/*.vtc)"
AC_SUBST(VTC_TESTS)
AM_SUBST_NOTMAKE(VTC_TESTS)

VMOD_TESTS="$(cd $srcdir/vmod && echo tests/*.vtc)"
AC_SUBST(VMOD_TESTS)
AM_SUBST_NOTMAKE(VMOD_TESTS)

AC_ARG_WITH([contrib],
	[AS_HELP_STRING([--with-contrib],
		[Build Varnish with external contributions.])])

AM_CONDITIONAL([WITH_CONTRIB], [test "$with_contrib" = yes])
CONTRIB_TESTS="$(cd $srcdir/contrib && echo tests/*.vtc)"
AC_SUBST(CONTRIB_TESTS)
AM_SUBST_NOTMAKE(CONTRIB_TESTS)

AM_COND_IF([WITH_CONTRIB], [
	AC_DEFINE([WITH_CONTRIB], [1],
		[Define to 1 when Varnish is built with contributions.])
])

# Make sure this include dir exists
AC_CONFIG_COMMANDS([mkdir], [$MKDIR_P doc/sphinx/include])

# Generate output
AC_CONFIG_FILES([
    Makefile
    bin/Makefile
    bin/varnishadm/Makefile
    bin/varnishd/Makefile
    bin/varnishlog/Makefile
    bin/varnishstat/Makefile
    bin/varnishtop/Makefile
    bin/varnishhist/Makefile
    bin/varnishtest/Makefile
    bin/varnishncsa/Makefile
    contrib/Makefile
    doc/Makefile
    doc/graphviz/Makefile
    doc/sphinx/Makefile
    doc/sphinx/conf.py
    etc/Makefile
    include/Makefile
    lib/Makefile
    lib/libvsc/Makefile
    lib/libvarnish/Makefile
    lib/libvarnishapi/Makefile
    lib/libvcc/Makefile
    lib/libvgz/Makefile
    man/Makefile
    varnishapi.pc
    varnishapi-uninstalled.pc
    vmod/Makefile
])

AC_OUTPUT
