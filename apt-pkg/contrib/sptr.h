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
   inline void operator =(T *N) {Ptr = N;}
   inline bool operator ==(std::nullptr_t) const {return Ptr == nullptr;}
   inline bool operator !=(std::nullptr_t) const {return Ptr != nullptr;}
   inline T*Get() const {return Ptr;}

   void reset(T * const NewPtr) {
      T * const Tmp = Ptr;
      Ptr = NewPtr;
      if (Tmp)
         delete Tmp;
   }

   // Prohibit copying; otherwise, two objects would own the pointer
   // and would try to delete it (on destruction). Actually,
   // -Werror=deprecated-copy -Werror=deprecated-copy-dtor should also
   // prohibit the use of the implictly-defined copy constructor and assignment
   // operator because we have a user-defined destructor (which is a hint that
   // there is some non-trivial resource management that is going on),
   // but the flags don't work in our case (with gcc10) for some reason...
   SPtr & operator= (const SPtr &) = delete;
   SPtr(const SPtr &) = delete;

   inline explicit SPtr(T * const Ptr) : Ptr(Ptr) {}
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
   inline void operator =(T *N) {Ptr = N;}
   inline bool operator ==(std::nullptr_t) const {return Ptr == nullptr;}
   inline bool operator !=(std::nullptr_t) const {return Ptr != nullptr;}
   inline T *Get() const {return Ptr;}

   void reset(T * const NewPtr) {
      T * const Tmp = Ptr;
      Ptr = NewPtr;
      if (Tmp)
         delete [] Tmp;
   }

   // Prohibit copying; otherwise, two objects would own the pointer
   // and would try to delete it (on destruction).
   SPtrArray & operator= (const SPtrArray &) = delete;
   SPtrArray(const SPtrArray &) = delete;

   inline explicit SPtrArray(T * const Ptr) : Ptr(Ptr) {}
   inline SPtrArray() : Ptr(0) {}
   inline ~SPtrArray() {delete [] Ptr;}
};

#endif
