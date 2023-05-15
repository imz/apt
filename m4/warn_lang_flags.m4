#!/usr/bin/m4
#
# Copyright (c) 2016-2018 The strace developers.
# All rights reserved.
#
# SPDX-License-Identifier: LGPL-2.1-or-later

AC_DEFUN([apt_WARN_LANG_FLAGS], [dnl
gl_WARN_ADD([-Wall])
# Prohibit implicit copy assignment operators or constructors
# if some special resource management is possibly needed:
gl_WARN_ADD([-Wdeprecated-copy])
gl_WARN_ADD([-Wdeprecated-copy-dtor])
gl_WARN_ADD([-Wextra])
gl_WARN_ADD([-Wformat-security])
gl_WARN_ADD([-Wimplicit-fallthrough=5])
gl_WARN_ADD([-Wlogical-op])
gl_WARN_ADD([-Wmissing-field-initializers])
# To avoid some errors on API change:
gl_WARN_ADD([-Woverloaded-virtual])
# A style enforcement: always use the keyword, which helps to avoid API misuse:
gl_WARN_ADD([-Wsuggest-override])
gl_WARN_ADD([-Wwrite-strings])
gl_WARN_ADD([-Wno-unused-parameter])
AC_ARG_ENABLE([Werror],
  [AS_HELP_STRING([--enable-Werror], [turn on -Werror option])],
  [case $enableval in
     yes) gl_WARN_ADD([-Werror]) ;;
     no)  ;;
     *)   AC_MSG_ERROR([bad value $enableval for Werror option]) ;;
   esac]
)
AS_VAR_PUSHDEF([apt_WARN_FLAGS], [WARN_[]_AC_LANG_PREFIX[]FLAGS])dnl
AC_SUBST(apt_WARN_FLAGS)
AS_VAR_POPDEF([apt_WARN_FLAGS])
])

# TODO: prepare warning flags for LTO in a similar way at one place here, too.
# They have to go into LDFLAGS and be prefixed with "-Wc,". Why:
#
# When it's time to link, in our build process, libtool calls g++ for linking,
# which internally uses a tool like ld and passes it "-Wl," flags.
#
# libtool is used with CXXFLAGS and LDFLAGS for the target; libtool
# filters flags, and then invokes g++: normal "-W" flags do not get
# through, but those prefixed with "-Wc," do.
# -- https://www.gnu.org/software/libtool/manual/libtool.html#Stripped-link-flags
# ("-Wl,"-prefixed flags would also pass through, but to the underlying ld;
# however, the usual warning flags would make no sense for ld, unlike for g++.)
#
# We could use the same C++ warning flags (with the prefix)
# or prepare a special list.
