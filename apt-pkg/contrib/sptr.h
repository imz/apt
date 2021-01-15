// Description								/*{{{*/
// $Id: sptr.h,v 1.1 2002/07/23 17:54:51 niemeyer Exp $
/* ######################################################################

   Trivial non-ref counted 'smart pointer'

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

template <class T>
class SPtr
{
   public:
   T *Ptr;

   inline T *operator ->() const {return Ptr;}
   inline T &operator *() const {return *Ptr;}
   inline operator T *() const {return Ptr;}
   inline operator void *() const {return Ptr;}
   inline T *UnGuard() {T *Tmp = Ptr; Ptr = 0; return Tmp;}
   inline bool operator ==(std::nullptr_t) const {return Ptr == nullptr;}
   inline bool operator !=(std::nullptr_t) const {return Ptr != nullptr;}
   inline T*Get() const {return Ptr;}

   inline SPtr(T *Ptr) : Ptr(Ptr) {}
   inline SPtr() : Ptr(0) {}
   inline ~SPtr() {delete Ptr;}
};

template <class T>
class SPtrArray
{
   public:
   T *Ptr;

   //inline T &operator *() const {return *Ptr;}
   inline operator T *() const {return Ptr;}
   inline operator void *() const {return Ptr;}
   inline T *UnGuard() {T *Tmp = Ptr; Ptr = 0; return Tmp;}
   //inline T &operator [](signed long I) const {return Ptr[I];}
   inline bool operator ==(std::nullptr_t) const {return Ptr == nullptr;}
   inline bool operator !=(std::nullptr_t) const {return Ptr != nullptr;}
   inline T *Get() const {return Ptr;}

   inline SPtrArray(T *Ptr) : Ptr(Ptr) {}
   inline SPtrArray() : Ptr(0) {}
   inline ~SPtrArray() {delete [] Ptr;}
};

#endif
