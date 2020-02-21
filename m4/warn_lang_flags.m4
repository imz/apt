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
gl_WARN_ADD([-Wsuggest-override])
gl_WARN_ADD([-Wwrite-strings])
gl_WARN_ADD([-Wno-deprecated-copy])
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
