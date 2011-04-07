dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.54)
AC_INIT([apt],[0.5.15lorg2])
dnl Check the archs, we want the target type.
AC_CONFIG_AUX_DIR([buildlib])
AC_CANONICAL_HOST
AC_CONFIG_SRCDIR([configure.ac])
AM_INIT_AUTOMAKE([foreign subdir-objects dist-bzip2 1.7])
AM_MAINTAINER_MODE

AC_CONFIG_HEADER([include/config.h:buildlib/config.h.in])

dnl Check our C compiler
AC_PROG_CC
AM_PROG_CC_C_O
AC_ISC_POSIX

dnl Check for other programs
AC_PROG_CXX
AC_PROG_CPP
AC_SYS_LARGEFILE

case "$ac_cv_sys_file_offset_bits" in
	no | unknown) FILE_OFFSET_BITS_SUFFIX=;;
	*) FILE_OFFSET_BITS_SUFFIX="-$ac_cv_sys_file_offset_bits";;
esac
AC_SUBST(FILE_OFFSET_BITS_SUFFIX)

AC_PROG_LIBTOOL

dnl Checks for sockets
SAVE_LIBS="$LIBS"
LIBS=""
AC_SEARCH_LIBS(gethostbyname,nsl)
AC_SEARCH_LIBS(connect,socket)
SOCKETLIBS="$LIBS"
AC_SUBST(SOCKETLIBS)
LIBS="$SAVE_LIBS"

dnl Checks for gnutls
SAVE_LIBS="$LIBS"
LIBS=""
AC_CHECK_LIB(gnutls, gnutls_init, [], [AC_MSG_ERROR([library 'gnutls' is required for TLS support])])
TLSLIBS="-lgnutls"
AC_SUBST(TLSLIBS)
LIBS="$SAVE_LIBS"

dnl Checks for pthread -- disabled due to glibc bugs jgg
dnl AC_CHECK_LIB(pthread, pthread_create,[AC_DEFINE(HAVE_PTHREAD) PTHREADLIB="-lpthread"])
AC_SUBST(PTHREADLIB)
dnl if test "$PTHREADLIB" != "-lpthread"; then
dnl   AC_MSG_ERROR(failed: I need posix threads, pthread)
dnl fi

dnl Check for RPM executable path
AC_PATH_PROG(RPM_PATH,rpm,none)
if test "$RPM_PATH" = "none"; then
   AC_MSG_ERROR([RPM executable was not found in your system])
else
   AC_DEFINE_UNQUOTED([HAVE_RPM],1,[Define if RPM backend is available])

   dnl Check for RPM libraries and headers

   CPPFLAGS="$CPPFLAGS -I/usr/include/rpm"
   AC_CHECK_HEADER(rpm/rpmlib.h, [],
		   [AC_MSG_ERROR([Can't find rpmlib.h])])

   RPMLIBS="-lrpm -lrpmio"
   AC_SUBST(RPMLIBS)
fi

dnl CNC:2003-03-20
AC_MSG_CHECKING(for --disable-scripts)
AC_ARG_ENABLE([scripts],
	      AC_HELP_STRING(--disable-scripts, disable the extension system),
	      [enable_scripts="$enableval"],[enable_scripts="yes"])
if test "$enable_scripts" != "no"; then
  AC_MSG_RESULT(no)
  AC_DEFINE(WITH_LUA, 1,
	    [Define if you want to enable the extension system.])
else
  AC_MSG_RESULT(yes)
fi
AM_CONDITIONAL(WITH_LUA, test "$enable_scripts" != "no")

dnl Before configuring libtool check for --enable-static-progs option
AC_MSG_CHECKING(for --enable-static-progs)
AC_ARG_ENABLE(static-progs,
	      AC_HELP_STRING([--enable-static-progs],
			     [Build completely static (standalone) binaries.]),
	      [enable_static_progs="$enableval"],[enable_static_progs="no"])
AM_CONDITIONAL(COMPILE_STATIC, test "$enable_static_progs" = "yes")
if test "$enable_static_progs" = "yes" ; then
  AC_MSG_RESULT(yes)
else
  AC_MSG_RESULT(no)
fi

dnl Check for apt-shell dependencies
AC_CHECK_HEADER(readline/readline.h,
   [AC_CHECK_LIB(readline,rl_completion_matches,
      [compile_aptshell=yes])])
AM_CONDITIONAL(COMPILE_APTSHELL, test "$compile_aptshell" = "yes")

dnl Converts the ARCH to be something singular for this general CPU family
dnl This is often the dpkg architecture string.
AH_TEMPLATE(COMMON_CPU, [Define the common CPU])
AH_TEMPLATE(COMMON_OS, [Define the common OS])
AC_MSG_CHECKING(system architecture)
archset="`awk \" ! /^#|^\\\$/ { if(match(\\\"$host_cpu\\\",\\\"^\\\"\\\$1\\\"\\\$\\\")) {print \\\$2; exit}}\" $srcdir/buildlib/archtable`"
if test "x$archset" = "x"; then
  AC_MSG_ERROR(failed: use --host=)
fi
AC_MSG_RESULT($archset)
AC_DEFINE_UNQUOTED(COMMON_CPU,"$archset")

dnl Get a common name for the host OS - this is primarily only for HURD and is
dnl non fatal if it fails
AC_MSG_CHECKING(system OS)
osset="`awk \" ! /^#|^\\\$/ {if (match(\\\"$host_vendor-$host_os\\\",\\\$1)) {print \\\$2; exit}}\" $srcdir/buildlib/ostable`"
AC_MSG_RESULT($osset)
AC_DEFINE_UNQUOTED(COMMON_OS,"$osset")

AC_C_BIGENDIAN

dnl Check the sizes etc. of the architecture
dnl Cross compilers can either get a real C library or preload the cache
dnl with their size values.
AC_CHECK_SIZEOF(char)
AC_CHECK_SIZEOF(int)
AC_CHECK_SIZEOF(short)
AC_CHECK_SIZEOF(long)

dnl Check for debiandoc
dnl AC_PATH_PROG(DEBIANDOC_HTML,debiandoc2html)
dnl AC_PATH_PROG(DEBIANDOC_TEXT,debiandoc2text)

dnl Check for the SGML tools needed to build man pages
AC_PATH_PROG(DOCBOOK2MAN,docbook2man)
AC_MSG_CHECKING([for --disable-docs])
AC_ARG_ENABLE([docs],
	      AC_HELP_STRING([--disable-docs],
			     [do not compile documentation]),
	      [enable_docs="$enableval"], [enable_docs="yes"])
if test "$enable_docs" != "no"; then
	AC_MSG_RESULT(no)
else
	AC_MSG_RESULT(yes)
fi
AM_CONDITIONAL(COMPILE_MANPAGES, test -n "$DOCBOOK2MAN" -a "$enable_docs" != "no")

AM_GNU_GETTEXT([external])

rc_GLIBC_VER
rc_LIBSTDCPP_VER
rc_CXX11ABI

AC_OUTPUT([
	  Makefile
	  buildlib/Makefile
	  apt-pkg/Makefile
	  methods/Makefile
	  cmdline/Makefile
	  doc/Makefile
	  ])
