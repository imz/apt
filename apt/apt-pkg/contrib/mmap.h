// Description								/*{{{*/
// $Id: mmap.h,v 1.1 2002/07/23 17:54:51 niemeyer Exp $
/* ######################################################################

   MMap Class - Provides 'real' mmap or a faked mmap using read().

   The purpose of this code is to provide a generic way for clients to
   access the mmap function. In enviroments that do not support mmap
   from file fd's this function will use read and normal allocated
   memory.

   Writing to a public mmap will always fully comit all changes when the
   class is deleted. Ie it will rewrite the file, unless it is readonly

   The DynamicMMap class is used to help the on-disk data structure
   generators. It provides a large allocated workspace and members
   to allocate space from the workspace in an effecient fashion.

   This source is placed in the Public Domain, do with it what you will
   It was originally written by Jason Gunthorpe.

   ##################################################################### */
									/*}}}*/
#ifndef PKGLIB_MMAP_H
#define PKGLIB_MMAP_H

#ifdef __GNUG__
#pragma interface "apt-pkg/mmap.h"
#endif

#include <string>
#include <limits>
#include <optional>

#include <apt-pkg/fileutl.h>

using std::string;

/* This should be a 32 bit type, larger tyes use too much ram and smaller
   types are too small. Where ever possible 'unsigned long' should be used
   instead of this internal type */
typedef unsigned int map_ptrloc;

class MMap
{
   protected:

   unsigned long Flags;
   unsigned long iSize;
   void *Base;

   bool Map(FileFd &Fd);
   bool Close(bool DoSync = true);

   public:

   enum OpenFlags {NoImmMap = (1<<0),Public = (1<<1),ReadOnly = (1<<2),
                   UnMapped = (1<<3)};

   // Simple accessors
   inline operator void *() {return Base;};
   inline void *Data() {return Base;};
   inline unsigned long Size() {return iSize;};

   // File manipulators
   bool Sync();
   bool Sync(unsigned long Start,unsigned long Stop);

   MMap(FileFd &F,unsigned long Flags);
   explicit MMap(unsigned long Flags);
   virtual ~MMap();
};

template<typename T> class PtrDiff;
template<typename T> T*& operator+=(T* &lhs, PtrDiff<T>);

template<typename T>
class PtrDiff
{
   unsigned long value;

   friend T*& operator+= <>(T* &lhs, PtrDiff<T>);

   explicit PtrDiff(const unsigned long ptrdiff): value(ptrdiff) { }

   public:

   static inline PtrDiff<T> ExtendAndGetAligned(unsigned long &ptrdiff)
   {
      const unsigned long aln = sizeof(T);

      // FIXME: what if ptrdiff overflows?
      ptrdiff += aln - (ptrdiff%aln ? : aln);

      // Using a private constructor, which is private for safety:
      // one cannot convert arbitrary value to a typed PtrDiff.
      return PtrDiff<T>(ptrdiff/aln);
   }

   PtrDiff(T * const ptr, T * const base)
   {
      value = ptr - base;
   }
};

template<typename T>
inline T*& operator+=(T* &lhs, const PtrDiff<T> diff)
{
   // FIXME: what if lhs overflows?
   return lhs += diff.value;
}

template<typename T>
inline T* operator+(T* base, const PtrDiff<T> diff)
{
   // FIXME: what if base overflows?
   return base += diff;
}

/* Operations on a void* base are also quite safe.
   (FIXME: what if the void* ptr is not optimally aligned? Not as we'd wish?
   If we want to check this, we should do this on type level.)
   And are useful to write shorter expressions.
 */

template<typename T>
inline T* operator+(void* const base, const PtrDiff<T> diff)
{
   return static_cast<T*>(base) + diff;
}

class DynamicMMap : public MMap
{
   public:

   // This is the allocation pool structure
   struct Pool
   {
      unsigned long ItemSize;
      unsigned long Start;
      unsigned long Count;
   };

   protected:

   FileFd *Fd;
   unsigned long WorkSpace;
   Pool *Pools;
   unsigned int PoolCount;

   public:

   // Allocation
   template<typename T>
   std::optional<PtrDiff<T>> RawAllocateArray(unsigned long Count);
   std::optional<unsigned long> Allocate(unsigned long ItemSize);
   std::optional<PtrDiff<char>> WriteString(const char *String,unsigned long Len = std::numeric_limits<unsigned long>::max());
   inline std::optional<PtrDiff<char>> WriteString(const string &S) {return WriteString(S.c_str(),S.length());};
   void UsePools(Pool &P,unsigned int Count) {Pools = &P; PoolCount = Count;};

   DynamicMMap(FileFd &F,unsigned long Flags,unsigned long WorkSpace = 2*1024*1024);
   DynamicMMap(unsigned long Flags,unsigned long WorkSpace = 2*1024*1024);
   virtual ~DynamicMMap();
};

#endif
