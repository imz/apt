// Description								/*{{{*/
// $Id: sptr.h,v 1.1 2002/07/23 17:54:51 niemeyer Exp $
/* ######################################################################

   Trivial non-ref counted 'smart pointer'
   FIXME: replace its uses in our code simply by std::unique_ptr for clarity
   (to new people reading the code).

   This is really only good to eliminate
     {
       delete Foo;
       return;
     }

   Blocks from functions.

   I think G++ has become good enough that doing this won't have much
   code size implications.

   ##################################################################### */
									/*}}}*/
#ifndef SMART_POINTER_H
#define SMART_POINTER_H

#include <memory>

template <class T>
using SPtr = std::unique_ptr<T>;

template <class T>
using SPtrArray = std::unique_ptr<T[]>;

#endif
