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
#include <limits>

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

/* SafeAssign_u - Set Var to Value if it won't be out of Var's limits.

   _u is a reminder that this function has been implemented only for
   unsigned arguments (Value) for simplicity.
 */
template<typename to_t, typename from_t>
[[nodiscard]]
constexpr bool SafeAssign_u(to_t &Var, const from_t &Value)
{
   // Check that Value is in the range of Var's type.
   // (Let's avoid complications with signed comparison & conversion.)
   static_assert(std::is_unsigned_v<from_t>,
                 "we assume the arg is unsigned to avoid complications");
   if (std::numeric_limits<to_t>::max() < Value)
      return false;

   // After the check above, -Wconversion and -Wsign-conversion warnings here
   // would be false, therefore we use static_cast below to suppress them.

   Var = static_cast<to_t>(Value);

   return true;
}

#endif
