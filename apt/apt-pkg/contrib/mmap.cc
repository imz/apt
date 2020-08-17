// Description								/*{{{*/
// $Id: mmap.cc,v 1.1 2002/07/23 17:54:51 niemeyer Exp $
/* ######################################################################

   MMap Class - Provides 'real' mmap or a faked mmap using read().

   MMap cover class.

   Some broken versions of glibc2 (libc6) have a broken definition
   of mmap that accepts a char * -- all other systems (and libc5) use
   void *. We can't safely do anything here that would be portable, so
   libc6 generates warnings -- which should be errors, g++ isn't properly
   strict.

   The configure test notes that some OS's have broken private mmap's
   so on those OS's we can't use mmap. This means we have to use
   configure to test mmap and can't rely on the POSIX
   _POSIX_MAPPED_FILES test.

   ##################################################################### */
									/*}}}*/
// Include Files							/*{{{*/
#ifdef __GNUG__
#pragma implementation "apt-pkg/mmap.h"
#endif

#include <config.h>

#define _BSD_SOURCE
#include <apt-pkg/mmap.h>
#include <apt-pkg/error.h>

#include <apti18n.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cassert>
   									/*}}}*/

// MMap::MMap - Constructor						/*{{{*/
// ---------------------------------------------------------------------
/* */
MMap::MMap(FileFd &F,unsigned long Flags) : Flags(Flags), iSize(0),
                     Base(nullptr)
{
   if ((Flags & NoImmMap) != NoImmMap)
      Map(F);
}
									/*}}}*/
// MMap::MMap - Constructor						/*{{{*/
// ---------------------------------------------------------------------
/* */
MMap::MMap(unsigned long Flags) : Flags(Flags), iSize(0),
                     Base(nullptr)
{
}
									/*}}}*/
// MMap::~MMap - Destructor						/*{{{*/
// ---------------------------------------------------------------------
/* */
MMap::~MMap()
{
   Close();
}
									/*}}}*/
// MMap::Map - Perform the mapping					/*{{{*/
// ---------------------------------------------------------------------
/* */
bool MMap::Map(FileFd &Fd)
{
   {
      unsigned long long EndOfFile = Fd.Size();
      if (EndOfFile > SIZE_MAX)
         return _error->Error(_("File of %llu bytes is too large for mmap(2)"),
                              EndOfFile);
      iSize = EndOfFile;
   }

   // Set the permissions.
   int Prot = PROT_READ;
   int Map = MAP_SHARED;
   if ((Flags & ReadOnly) != ReadOnly)
      Prot |= PROT_WRITE;
   if ((Flags & Public) != Public)
      Map = MAP_PRIVATE;

   if (iSize == 0)
      return _error->Error(_("Can't mmap an empty file"));

   // Map it.
   Base = mmap(0,iSize,Prot,Map,Fd.Fd(),0);
   if (Base == MAP_FAILED)
      return _error->Errno("mmap",_("Couldn't make mmap of %lu bytes"),iSize);

   return true;
}
									/*}}}*/
// MMap::Close - Close the map						/*{{{*/
// ---------------------------------------------------------------------
/* */
bool MMap::Close(bool DoSync)
{
   if ((Flags & UnMapped) == UnMapped || Base == 0 || iSize == 0)
      return true;

   if (DoSync == true)
      Sync();

   if (munmap((char *)Base,iSize) != 0)
      _error->Warning("Unable to munmap");

   iSize = 0;
   Base = 0;
   return true;
}
									/*}}}*/
// MMap::Sync - Syncronize the map with the disk			/*{{{*/
// ---------------------------------------------------------------------
/* This is done in syncronous mode - the docs indicate that this will
   not return till all IO is complete */
bool MMap::Sync()
{
   if ((Flags & UnMapped) == UnMapped)
      return true;

#ifdef _POSIX_SYNCHRONIZED_IO
   if ((Flags & ReadOnly) != ReadOnly)
      if (msync((char *)Base,iSize,MS_SYNC) != 0)
	 return _error->Errno("msync","Unable to write mmap");
#endif
   return true;
}
									/*}}}*/
// MMap::Sync - Syncronize a section of the file to disk		/*{{{*/
// ---------------------------------------------------------------------
/* */
bool MMap::Sync(unsigned long Start,unsigned long Stop)
{
   if ((Flags & UnMapped) == UnMapped)
      return true;

#ifdef _POSIX_SYNCHRONIZED_IO
   unsigned long PSize = sysconf(_SC_PAGESIZE);
   if ((Flags & ReadOnly) != ReadOnly)
      if (msync((char *)Base+(int)(Start/PSize)*PSize,Stop - Start,MS_SYNC) != 0)
	 return _error->Errno("msync","Unable to write mmap");
#endif
   return true;
}
									/*}}}*/

// DynamicMMap::DynamicMMap - Constructor				/*{{{*/
// ---------------------------------------------------------------------
/* */
DynamicMMap::DynamicMMap(FileFd &F,unsigned long Flags,unsigned long WorkSpace) :
             MMap(F,Flags | NoImmMap), Fd(&F), WorkSpace(WorkSpace)
{
   if (_error->PendingError() == true)
      return;

   unsigned long long EndOfFile = Fd->Size();
   if (EndOfFile < WorkSpace)
   {
      Fd->Seek(WorkSpace - 1);
      char C = 0;
      Fd->Write(&C,sizeof(C));
   }

   Map(F); // sets iSize to current Fd->Size()
   WorkSpace = iSize; // WorkSpace may be increased here to Fd->Size()
   iSize = EndOfFile; // decrease it to the space initially used in the file
}
									/*}}}*/
// DynamicMMap::DynamicMMap - Constructor for a non-file backed map	/*{{{*/
// ---------------------------------------------------------------------
/* This is just a fancy malloc really.. */
DynamicMMap::DynamicMMap(unsigned long Flags,unsigned long WorkSpace) :
             MMap(Flags | NoImmMap | UnMapped), Fd(0), WorkSpace(WorkSpace)
{
   if (_error->PendingError() == true)
      return;

   Base = new unsigned char[WorkSpace];
   memset(Base,0,WorkSpace);
   iSize = 0;
}
									/*}}}*/
// DynamicMMap::~DynamicMMap - Destructor				/*{{{*/
// ---------------------------------------------------------------------
/* We truncate the file to the size of the memory data set */
DynamicMMap::~DynamicMMap()
{
   if (Fd == 0)
   {
      delete [] (unsigned char *)Base;
      return;
   }

   unsigned long long EndOfFile = iSize;
   iSize = WorkSpace;
   Close(false);
   ftruncate(Fd->Fd(),EndOfFile);
}
									/*}}}*/
// DynamicMMap::RawAllocate - Allocate a raw chunk of unaligned space	/*{{{*/
// ---------------------------------------------------------------------
/* This allocates a block of memory aligned to the given size */
std::optional<unsigned long> DynamicMMap::RawAllocate(unsigned long Size,unsigned long Aln)
{
   unsigned long Result = iSize;
   if (Aln != 0)
      Result += Aln - (iSize%Aln ? : Aln);

   // Just in case error check
   if (Result + Size > WorkSpace)
   {
      _error->Error("Dynamic MMap ran out of room");
      return std::nullopt;
   }

   iSize = Result + Size;

   return Result;
}
									/*}}}*/
// DynamicMMap::Allocate - Pooled aligned allocation			/*{{{*/
// ---------------------------------------------------------------------
/* This allocates an Item of size ItemSize so that it is aligned to its
   size in the file. */
std::optional<unsigned long> DynamicMMap::Allocate(unsigned long ItemSize)
{
   assert(ItemSize != 0); /* Actually, we are always called with sizeof(...)
                             compile-time non-zero constant as the argument.
                          */

   // Look for a matching pool entry
   Pool *I;
   Pool *Empty = 0;
   for (I = Pools; I != Pools + PoolCount; I++)
   {
      if (I->ItemSize == 0)
	 Empty = I;
      if (I->ItemSize == ItemSize)
	 break;
   }

   // No pool is allocated, use an unallocated one
   if (I == Pools + PoolCount)
   {
      // Woops, we ran out, the calling code should allocate more.
      if (Empty == 0)
      {
	 _error->Error("Ran out of allocation pools");
	 return std::nullopt;
      }

      I = Empty;
      I->ItemSize = ItemSize;
      I->Count = 0;
   }

   // Out of space, allocate some more
   if (I->Count == 0)
   {
      I->Count = 20*1024/ItemSize;
      auto idxResult = RawAllocate(I->Count*ItemSize,ItemSize);

      // Has the allocation failed?
      if (!idxResult)
         return std::nullopt;

      I->Start = *idxResult;
   }

   I->Count--;
   unsigned long Result = I->Start;
   I->Start += ItemSize;
   return Result/ItemSize;
}
									/*}}}*/
// DynamicMMap::WriteString - Write a string to the file		/*{{{*/
// ---------------------------------------------------------------------
/* Strings are not aligned to anything */
std::optional<unsigned long> DynamicMMap::WriteString(const char *String,
				       unsigned long Len)
{
   if (Len == std::numeric_limits<decltype(Len)>::max())
      Len = strlen(String);

   /* Len != std::numeric_limits<decltype(Len)>::max()
      because the final 0 in String fits in the memory;
      so Len+1 will never overflow.
   */

   const auto Result = RawAllocate(Len+1,0);

   if (!Result)
      return std::nullopt;

   char * const dest = static_cast<char *>(Base) + *Result;
   memcpy(dest,String,Len);
   dest[Len] = 0;

   return Result;
}
									/*}}}*/
