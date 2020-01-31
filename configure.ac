AC_PREREQ(2.59)
AC_COPYRIGHT([Copyright (c) 2006 Verdens Gang AS
Copyright (c) 2006-2019 Varnish Software])
AC_REVISION([$Id$])
AC_INIT([Varnish], [6.0.6], [varnish-dev@varnish-cache.org])
AC_CONFIG_SRCDIR(include/miniobj.h)
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AC_USE_SYSTEM_EXTENSIONS

# save command line CFLAGS for use in VCC_CC (to pass through things like -m64)
# and make distcheck configure
OCFLAGS="$CFLAGS"
EXTCFLAGS="$CFLAGS"
AC_SUBST(EXTCFLAGS)

AC_CANONICAL_SYSTEM
AC_LANG(C)

AM_MAINTAINER_MODE([disable])
AM_INIT_AUTOMAKE([1.11 foreign color-tests parallel-tests])
AM_SILENT_RULES([yes])
AC_DISABLE_STATIC
AC_PROG_LIBTOOL

# Checks for programs.
AC_GNU_SOURCE
AC_PROG_CC
AC_PROG_CC_STDC

AX_PTHREAD(,[AC_MSG_ERROR([Could not configure pthreads support])])

LIBS="$PTHREAD_LIBS $LIBS"
CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
CC="$PTHREAD_CC"

AC_PROG_INSTALL

AC_ARG_WITH([rst2man],
  AS_HELP_STRING([--with-rst2man=PATH], [Location of rst2man (auto)]),
  [RST2MAN="$withval"],
  AC_CHECK_PROGS(RST2MAN,
    [rst2man rst2man.py rst2man-3.6 rst2man-2.7],
    [no]))
if test "x$RST2MAN" = "xno"; then
  AC_MSG_ERROR(
    [rst2man is needed to build Varnish, please install python-docutils.])
fi

AC_ARG_WITH([sphinx-build],
  AS_HELP_STRING([--with-sphinx-build=PATH], [Location of sphinx-build (auto)]),
  [SPHINX="$withval"],
  AC_CHECK_PROGS(SPHINX,
    [sphinx-build sphinx-build-3.6 sphinx-build-2.7],
    [no]))
if test "x$SPHINX" = "xno"; then
  AC_MSG_ERROR(
    [sphinx-build is needed to build Varnish, please install python-sphinx.])
fi

AC_ARG_WITH([rst2html],
               AS_HELP_STRING([--with-rst2html=PATH],
                              [Location of rst2html (auto)]),
               [RST2HTML="$withval"],
               [AC_CHECK_PROGS(RST2HTML, [rst2html rst2html.py], "no")
	        if test "x$RST2HTML" = "xno"; then
		   AC_MSG_WARN([rst2html not found - not building changelog])
		fi])
AM_CONDITIONAL(HAVE_RST2HTML,[test "x$RST2HTML" != "xno"])

AC_ARG_WITH([dot],
  AS_HELP_STRING([--with-dot=PATH],
		 [Location of the dot tool from graphviz (auto)]),
  [DOT="$withval"],
  [AC_CHECK_PROGS(DOT, [dot], [no])
   if test "x$DOT" = "xno"; then
     AC_MSG_WARN(
       [dot not found - build will fail if svg files are out of date.])
   fi])
AM_CONDITIONAL(HAVE_DOT,[test "x$DOT" != "xno"])

# Define VMOD flags
_VARNISH_VMOD_LDFLAGS

# Check for python.
_VARNISH_CHECK_PYTHON

# Check for libraries.
_VARNISH_SEARCH_LIBS(pthread, pthread_create, [thr pthread c_r])
_VARNISH_CHECK_LIB(rt, clock_gettime)
_VARNISH_CHECK_LIB(dl, dlopen)
_VARNISH_CHECK_LIB(socket, socket)
_VARNISH_CHECK_LIB(nsl, getaddrinfo)

AC_SUBST(NET_LIBS, "${SOCKET_LIBS} ${NSL_LIBS}")

# Userland slab allocator from Solaris, ported to other systems
AC_CHECK_HEADERS([umem.h])

# XXX: This _may_ be for OS/X
AC_CHECK_LIBM
AC_SUBST(LIBM)

m4_ifndef([PKG_PROG_PKG_CONFIG], [m4_fatal([pkg.m4 missing, please install pkg-config])])
PKG_PROG_PKG_CONFIG
if test -n $PKG_CONFIG; then
   PKG_CHECK_MODULES([PCRE], [libpcre])
else
   AC_CHECK_PROG(PCRE_CONFIG, pcre-config, pcre-config)
   AC_ARG_WITH(pcre-config,
               AS_HELP_STRING([--with-pcre-config=PATH],
                              [Location of PCRE pcre-config (auto)]),
               [pcre_config="$withval"],
               [pcre_config=""])

  if test "x$pcre_config" != "x" ; then
    AC_MSG_CHECKING(for $pcre_config)

    if test -f $pcre_config ; then
      PCRE_CONFIG=$pcre_config
      AC_MSG_RESULT(yes)
    else
      AC_MSG_RESULT(no - searching PATH)
    fi
  fi
  if test "x$PCRE_CONFIG" = "x"; then
    AC_CHECK_PROGS(PCRE_CONFIG, pcre-config)
  fi
  PCRE_CFLAGS=`$PCRE_CONFIG --cflags`
  PCRE_LIBS=`$PCRE_CONFIG --libs`
fi
AC_SUBST(PCRE_CFLAGS)
AC_SUBST(PCRE_LIBS)

# --enable-pcre-jit
AC_ARG_ENABLE(pcre-jit,
  AS_HELP_STRING([--enable-pcre-jit],
    [use the PCRE JIT compiler (default is YES)]),
    [],
    [enable_pcre_jit=yes])
if test "$enable_pcre_jit" = yes; then
  AC_MSG_CHECKING(for PCRE JIT usability)
  save_CFLAGS="${CFLAGS}"
  CFLAGS="${CFLAGS} ${PCRE_CFLAGS}"
  save_LIBS="${LIBS}"
  LIBS="${LIBS} ${PCRE_LIBS}"
  AC_RUN_IFELSE(
    [AC_LANG_PROGRAM([[
#include <pcre.h>
#if PCRE_MAJOR != 8 || PCRE_MINOR < 32
#error no jit
#endif
    ]],[[
const char *error;
pcre *re;
int erroroffset;
re = pcre_compile(".", 0, &error, &erroroffset, NULL);
if (!pcre_study(re, PCRE_STUDY_JIT_COMPILE, &error))
  return (1);
    ]])],
    [AC_MSG_RESULT(yes)
     AC_DEFINE([USE_PCRE_JIT], [1], [Use the PCRE JIT compiler])
    ],
    [AC_MSG_RESULT(no)]
  )
  CFLAGS="${save_CFLAGS}"
  LIBS="${save_LIBS}"
fi


AC_CHECK_HEADERS([edit/readline/readline.h],
	[AC_DEFINE([HAVE_LIBEDIT], [1], [Define if we have libedit])
	LIBEDIT_LIBS="-ledit"],

	[PKG_CHECK_MODULES([LIBEDIT], [libedit],
		# having the module does not imply having the header
		[AC_CHECK_HEADERS([editline/readline.h],
				  [AC_DEFINE([HAVE_LIBEDIT], [1], [Define if we have libedit])],
				  [AC_MSG_ERROR([Found libedit, but header file is missing. Hint: Install dev package?])])],
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
AC_SUBST([CURSES_LIBS])

AC_CHECK_HEADERS([ncursesw/curses.h ncursesw.h ncurses/curses.h ncurses.h curses.h])

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([sys/endian.h])
AC_CHECK_HEADERS([sys/filio.h])
AC_CHECK_HEADERS([sys/mount.h], [], [], [#include <sys/param.h>])
AC_CHECK_HEADERS([sys/personality.h])
AC_CHECK_HEADERS([sys/statvfs.h])
AC_CHECK_HEADERS([sys/vfs.h])
AC_CHECK_HEADERS([endian.h])
AC_CHECK_HEADERS([pthread_np.h], [], [], [#include <pthread.h>])
AC_CHECK_HEADERS([priv.h])
AC_CHECK_HEADERS([fnmatch.h], [], [AC_MSG_ERROR([fnmatch.h is required])])

# Checks for library functions.
_VARNISH_CHECK_EXPLICIT_BZERO
AC_CHECK_FUNCS([nanosleep])
AC_CHECK_FUNCS([setppriv])
AC_CHECK_FUNCS([fallocate])
AC_CHECK_FUNCS([closefrom])
AC_CHECK_FUNCS([sigaltstack])
AC_CHECK_FUNCS([getpeereid])
AC_CHECK_FUNCS([getpeerucred])
AC_CHECK_FUNCS([fnmatch], [], [AC_MSG_ERROR([fnmatch(3) is required])])

save_LIBS="${LIBS}"
LIBS="${PTHREAD_LIBS}"
AC_CHECK_FUNCS([pthread_set_name_np])
AC_CHECK_FUNCS([pthread_setname_np])
AC_CHECK_FUNCS([pthread_mutex_isowned_np])
LIBS="${save_LIBS}"

# Support for visibility attribute
save_CFLAGS="${CFLAGS}"
CFLAGS="${CFLAGS} -Werror"

AC_CACHE_CHECK([whether we have support for visibility attributes],
	[ac_cv_have_viz],
	[AC_RUN_IFELSE(
		[AC_LANG_PROGRAM([[
			#if ((__GNUC__-0) * 10 + __GNUC_MINOR__-0 >= 33)
			#  define ZLIB_INTERNAL __attribute__((visibility ("hidden")))
			#else
			#  define ZLIB_INTERNAL
			#endif
			int ZLIB_INTERNAL foo;
		]],[])],
	[ac_cv_have_viz=yes],
	[ac_cv_have_viz=no])
])
if test "$ac_cv_have_viz" = no; then
	libvgz_extra_cflags="-DNO_VIZ"
	AC_SUBST(libvgz_extra_cflags)
fi
CFLAGS="${save_CFLAGS}"

if test "x$GCC" = "xyes"; then
	libvgz_extra_cflags="${libvgz_extra_cflags} -Wno-unknown-warning-option -Wno-implicit-fallthrough"
	AC_SUBST(libvgz_extra_cflags)
fi

SAN_CFLAGS=
SAN_LDFLAGS=
UBSAN_CFLAGS=
UBSAN_LDFLAGS=
AC_ARG_ENABLE(ubsan,
	AS_HELP_STRING([--enable-ubsan],[enable undefined behavior sanitizer (default is NO)]),
	UBSAN_FLAGS="-fsanitize=undefined")

TSAN_CFLAGS=
TSAN_LDFLAGS=
AC_ARG_ENABLE(tsan,
	AS_HELP_STRING([--enable-tsan],[enable thread sanitizer (default is NO)]),
	TSAN_FLAGS="-fsanitize=thread")

ASAN_CFLAGS=
ASAN_LDFLAGS=
AC_ARG_ENABLE(asan,
	AS_HELP_STRING([--enable-asan],[enable address sanitizer (default is NO)]),
	ASAN_FLAGS="-fsanitize=address"
	AX_CHECK_COMPILE_FLAG([-fsanitize=address -fsanitize-address-use-after-scope],
			      [ASAN_FLAGS="${ASAN_FLAGS} -fsanitize-address-use-after-scope"]))

MSAN_CFLAGS=
MSAN_LDFLAGS=
AC_ARG_ENABLE(msan,
	AS_HELP_STRING([--enable-msan],[enable memory sanitizer (default is NO)]),
	MSAN_FLAGS="-fsanitize=memory")

if test "x$UBSAN_FLAGS$TSAN_FLAGS$ASAN_FLAGS$MSAN_FLAGS" != "x"; then
	SAN_CFLAGS="-D__SANITIZER=1 ${UBSAN_FLAGS} ${TSAN_FLAGS} ${ASAN_FLAGS} ${MSAN_FLAGS} -fPIC -fPIE -fno-omit-frame-pointer"
	SAN_LDFLAGS="${UBSAN_FLAGS} ${TSAN_FLAGS} ${ASAN_FLAGS} ${MSAN_FLAGS}"
	save_CFLAGS="${CFLAGS}"
	CFLAGS="${CFLAGS} -Werror=unused-command-line-argument"
	AX_CHECK_LINK_FLAG([-pie], [SAN_LDFLAGS="${SAN_LDFLAGS} -pie"])
	CFLAGS="${save_CFLAGS}"
	case $CC in
	gcc*)
		SAN_CFLAGS="${SAN_CFLAGS} -fuse-ld=gold"
		;;
	esac
fi
AC_SUBST(SAN_CFLAGS)
AC_SUBST(SAN_LDFLAGS)

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
AC_SEARCH_LIBS(backtrace, [execinfo], [], [
   AC_MSG_ERROR([Could not find backtrace() support])
])
# white lie - we don't actually test it
AC_MSG_CHECKING([whether daemon() works])
case $target in
*-*-darwin*)
	# present but not functional
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
		[Define to 1 to build the deprecated peristent storage.])
fi

AM_CONDITIONAL([WITH_PERSISTENT_STORAGE],
	[test "$with_persistent_storage" = yes])

AM_MISSING_HAS_RUN

AC_CHECK_DECL([SO_ACCEPTFILTER],
    AC_DEFINE(HAVE_ACCEPT_FILTERS,1,[Define to 1 if you have accept filters]),
    ,
    [
#include <sys/types.h>
#include <sys/socket.h>
    ]
)

# Older Solaris versions define SO_{RCV,SND}TIMEO, but do not
# implement them.
#
# Varnish will build and run without these, but connections will not
# time out, which may leave Varnish vulnerable to denail-of-service
# attacks which would not be possible on other platforms.
#
# Newer Solaris releases with the Volo framework (Solaris 11,
# Opensolaris starting with onnv_106) do support SO_{RCV,SND}TIMEO
# (see PSARC 2007/587, initially committed into onnv-gate /
# OpenSolaris 8348:4137e18bfaf0 Thu Dec 11 20:04:13 2008)

save_LIBS="${LIBS}"
LIBS="${LIBS} ${NET_LIBS}"
AC_CACHE_CHECK([whether SO_RCVTIMEO works],
  [ac_cv_so_rcvtimeo_works],
  [AC_RUN_IFELSE(
    [AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
    ]],[[
int s = socket(AF_INET, SOCK_STREAM, 0);
struct timeval tv = { 1, 0 };
if (s < 0 && errno == EPROTONOSUPPORT)
  s = socket(AF_INET6, SOCK_STREAM, 0);
if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) == 0) {
  socklen_t l = sizeof tv;
  if (getsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, &l) == 0) {
    return (l != sizeof tv);
  }
}
return 1;
    ]])],
    [ac_cv_so_rcvtimeo_works=yes],
    [ac_cv_so_rcvtimeo_works=no])
  ])
if test "$ac_cv_so_rcvtimeo_works" = yes; then
   AC_DEFINE([SO_RCVTIMEO_WORKS], [1], [Define if SO_RCVTIMEO works])
fi
LIBS="${save_LIBS}"

save_LIBS="${LIBS}"
LIBS="${LIBS} ${NET_LIBS}"
AC_CACHE_CHECK([whether SO_SNDTIMEO works],
  [ac_cv_so_sndtimeo_works],
  [AC_RUN_IFELSE(
    [AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
    ]],[[
int s = socket(AF_INET, SOCK_STREAM, 0);
struct timeval tv = { 1, 0 };
if (s < 0 && errno == EPROTONOSUPPORT)
  s = socket(AF_INET6, SOCK_STREAM, 0);
if (setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv) == 0) {
  socklen_t l = sizeof tv;
  if (getsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, &l) == 0) {
    return (l != sizeof tv);
  }
}
return 1;
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
LIBS="${save_LIBS}"

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

# Run-time directory
VARNISH_STATE_DIR='${localstatedir}/varnish'
AC_SUBST(VARNISH_STATE_DIR)

# Default configuration directory.
pkgsysconfdir='${sysconfdir}/varnish'
AC_SUBST(pkgsysconfdir)

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

AX_CHECK_COMPILE_FLAG([-Wall],
     [CFLAGS="${CFLAGS} -Wall"
      OCFLAGS="${OCFLAGS} -Wall"])

AC_CHECK_DECL([__SUNPRO_C], [SUNCC="yes"], [SUNCC="no"])

if test "$SUNCC" = "yes" ; then
    SUNCC_CFLAGS=" \
	-errwarn=%all,no%E_EMPTY_TRANSLATION_UNIT \
	-errtags=yes \
	"
    AX_CHECK_COMPILE_FLAG([${SUNCC_CFLAGS}],
	[CFLAGS="${CFLAGS} ${SUNCC_CFLAGS}"
	 OCFLAGS="${OCFLAGS} ${SUNCC_CFLAGS}"])
else
    AX_CHECK_COMPILE_FLAG([-Werror],
	[CFLAGS="${CFLAGS} -Werror"
	 OCFLAGS="${OCFLAGS} -Werror"])
fi

AX_CHECK_COMPILE_FLAG([-Werror=unused-result],
    [CFLAGS="${CFLAGS} -Wno-error=unused-result"
     OCFLAGS="${OCFLAGS} -Wno-error=unused-result"],
    [AX_CHECK_COMPILE_FLAG([-Wunused-result],
     [CFLAGS="${CFLAGS} -Wno-unused-result"
      OCFLAGS="${OCFLAGS} -Wno-unused-result"])])

# This corresponds to FreeBSD's WARNS level 6
DEVELOPER_CFLAGS=" \
	-Werror \
	-Wall \
	-Wno-format-y2k \
	-W \
	-Wstrict-prototypes \
	-Wmissing-prototypes \
	-Wpointer-arith \
	-Wreturn-type \
	-Wcast-qual \
	-Wwrite-strings \
	-Wswitch \
	-Wshadow \
	-Wunused-parameter \
	-Wcast-align \
	-Wchar-subscripts \
	-Wnested-externs \
	-Wextra \
	-Wno-sign-compare \
"

# These are not compliable yet
DEVELOPER_GCC_CFLAGS="-Wold-style-definition -Wredundant-decls "
#DEVELOPER_CFLAGS="${DEVELOPER_CFLAGS} ${DEVELOPER_GCC_CFLAGS}"

# These are compilable
DEVELOPER_CLANG_CFLAGS="-Wmissing-variable-declarations -Wno-string-plus-int"
# -Wno-empty-body
#DEVELOPER_CFLAGS="${DEVELOPER_CFLAGS} ${DEVELOPER_CLANG_CFLAGS}"

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
	# warnings keeping the rest of Wextra
	AX_CHECK_COMPILE_FLAG([-Wno-missing-field-initializers],
		[DEVELOPER_CFLAGS="${DEVELOPER_CFLAGS} -Wno-missing-field-initializers"],
		[DEVELOPER_CFLAGS="${DEVELOPER_CFLAGS} -Wno-extra"],
		[])

	CFLAGS="${CFLAGS} ${DEVELOPER_CFLAGS}"
	OCFLAGS="${OCFLAGS} ${DEVELOPER_CFLAGS}"
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

# --enable-debugging-symbols
AC_ARG_ENABLE(debugging-symbols,
	AS_HELP_STRING([--enable-debugging-symbols],[enable debugging symbols (default is NO)]),
	if test "x$SUNCC" = "xyes" ; then
		CFLAGS="${CFLAGS} -O0 -g"
	else
		CFLAGS="${CFLAGS} -O0 -g -fno-inline"
	fi)

AC_SUBST(AM_LT_LDFLAGS)

# Command line for compiling VCL code.  I wish there were a simple way
# to figure this out dynamically without introducing a run-time
# dependency on libtool.
AC_ARG_VAR([VCC_CC], [C compiler command line for VCL code])
if test "$ac_cv_env_VCC_CC_set" = "set"; then
	VCC_CC="$ac_cv_env_VCC_CC_value"
else
	case $target in
	*-*-solaris*)
		case $PTHREAD_CC in
		*gcc*)
			VCC_CC="$PTHREAD_CC $OCFLAGS $PTHREAD_CFLAGS -fpic -shared -o %o %s"
			break
			;;
		*cc)
			VCC_CC="$PTHREAD_CC $OCFLAGS -errwarn=%all,no%E_STATEMENT_NOT_REACHED $PTHREAD_CFLAGS -Kpic -G -o %o %s"
			;;
		esac
		;;
	*-*-darwin*)
		VCC_CC="exec cc $OCFLAGS -dynamiclib -Wl,-undefined,dynamic_lookup -o %o %s"
		;;
	*)
		VCC_CC="exec $PTHREAD_CC $OCFLAGS $PTHREAD_CFLAGS -fpic -shared -Wl,-x -o %o %s"
		;;
	esac
fi

AC_DEFINE_UNQUOTED([VCC_CC],"$VCC_CC",[C compiler command line for VCL code])

# Stupid automake needs this
VTC_TESTS="$(cd $srcdir/bin/varnishtest && echo tests/*.vtc)"
AC_SUBST(VTC_TESTS)

VSC_SRC="$(cd $srcdir/bin/varnishd && echo *.vsc)"
VSC_GEN_C="$(echo $VSC_SRC | sed 's:\.vsc:.c:g')"
VSC_GEN_H="$(echo $VSC_SRC | sed 's:\.vsc:.h:g')"
# automake does not allow to AC_SUBST _SOURCES files (for a good
# reason), so we automate the cleanup at least
AC_SUBST(VSC_GEN_C)
AC_SUBST(VSC_GEN_H)

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
    doc/Makefile
    doc/graphviz/Makefile
    doc/sphinx/Makefile
    doc/sphinx/conf.py
    etc/Makefile
    include/Makefile
    lib/Makefile
    lib/libvarnish/Makefile
    lib/libvarnishapi/Makefile
    lib/libvcc/Makefile
    lib/libvgz/Makefile
    lib/libvmod_debug/Makefile
    lib/libvmod_std/Makefile
    lib/libvmod_directors/Makefile
    lib/libvmod_purge/Makefile
    lib/libvmod_vtc/Makefile
    lib/libvmod_blob/Makefile
    lib/libvmod_unix/Makefile
    lib/libvmod_proxy/Makefile
    man/Makefile
    varnishapi.pc
    varnishapi-uninstalled.pc
])
AC_OUTPUT
