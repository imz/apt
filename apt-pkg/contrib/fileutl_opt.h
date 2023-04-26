// Description								/*{{{*/
/* ######################################################################

   Additional File Utilities

   We couldn't place these ones into fileutl.h, because they use
   std::optional in their API, and not all clients of the library
   compile with -std=c++17, which is needed for it to be available.

   GetFileSize - Return the size of a file by path or report it missing

   These are handy abstractions for functions accepting filenames and
   they follow the APT representations of the types (i.e., size is unsigned).

   This source is placed in the Public Domain (as the original fileutl.h),
   do with it what you will.
   This addendum to fileutl.h was originally written by Ivan Zakharyaschev.

   ##################################################################### */
									/*}}}*/
#ifndef PKGLIB_FILEUTL_OPT_H
#define PKGLIB_FILEUTL_OPT_H

#include <apt-pkg/fileutl.h>

#include <optional>

std::optional<filesize> GetFileSize(const std::string &File);

#endif
