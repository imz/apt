// Description								/*{{{*/
/* ######################################################################

   Utilities for Arithmetics

   For now, here are mostly utilities to safely work with non-negative numbers.

   This source is placed in the Public Domain (as the original fileutl.h),
   do with it what you will.
   This addendum to fileutl.h was originally written by Ivan Zakharyaschev.

   ##################################################################### */
									/*}}}*/
#ifndef PKGLIB_ARITHUTL_H
#define PKGLIB_ARITHUTL_H

#include <type_traits>
#include <cassert>

/* NonnegAsU - Useful utility to work with a non-negative integer as unsigned.

   BTW, this is the common safest way--to first cast to the same-width
   unsigned type--whatever width one is ultimately extending the value to.
   (Especially, w.r.t. zero-extending, which is however not really important
   for non-negative values.)
 */
template<typename t>
constexpr std::make_unsigned_t<t> NonnegAsU(const t &X)
{
   assert(X >= 0);
   return static_cast<std::make_unsigned_t<t> >(X);
}

#endif
