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
#include <config.h>

#define _DEFAULT_SOURCE
#include <apt-pkg/mmap.h>
#include <apt-pkg/error.h>

#include <apti18n.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <type_traits>
#include <cassert>
									/*}}}*/

// MMap::MMap - Constructor						/*{{{*/
// ---------------------------------------------------------------------
/* */
MMap::MMap(FileFd &F,unsigned long const Flags) : Flags(Flags), iSize(0),
                     Base(nullptr)
{
   if ((Flags & NoImmMap) != NoImmMap)
      Map(F);
}
									/*}}}*/
// MMap::MMap - Constructor						/*{{{*/
// ---------------------------------------------------------------------
/* */
MMap::MMap(unsigned long const Flags) : Flags(Flags), iSize(0),
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
   /* Invalidate all fields so that they are not valid in case of failure. */
   Base = nullptr;
   iSize = 0;

   auto const EndOfFile = Fd.Size();

   // Check that the file size is in the range of size_t.
   static_assert(std::is_unsigned_v<decltype(EndOfFile)>,
                 "we want to rely that Fd::Size() is unsigned");
   if (EndOfFile > SIZE_MAX)
      return _error->Error(_("File of %ju bytes is too large for mmap(2)"),
                           static_cast<uintmax_t>(EndOfFile));
   // After the check above, -Wconversion and -Wsign-conversion warnings here
   // would be false, therefore we use static_cast below to suppress them.
   if (EndOfFile == 0)
      return _error->Error(_("Can't mmap an empty file"));

   // Set the permissions.
   int Prot = PROT_READ;
   int Map = MAP_SHARED;
   if ((Flags & ReadOnly) != ReadOnly)
      Prot |= PROT_WRITE;
   if ((Flags & Public) != Public)
      Map = MAP_PRIVATE;

   // Map it.
   Base = mmap(0,static_cast<size_t>(EndOfFile),Prot,Map,Fd.Fd(),0);
   if (Base == (void *)MAP_FAILED)
      return _error->Errno("mmap",_("Couldn't make mmap of %zu bytes"),
                           static_cast<size_t>(EndOfFile));

   iSize = static_cast<size_t>(EndOfFile);
   return true;
}
									/*}}}*/
// MMap::Close - Close the map						/*{{{*/
// ---------------------------------------------------------------------
/* */
bool MMap::Close(bool const DoSync)
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
bool MMap::Sync(unsigned long const Start,unsigned long const Stop)
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
DynamicMMap::DynamicMMap(FileFd &F,
                         unsigned long const Flags,
                         size_t const RequestedWorkSpace) :
   MMap(F,Flags | NoImmMap), /* NoImmMap to invoke MMap::Map() ourselves */
   Fd(&F),
   WorkSpace(0) // invalidate before all the structure is set up
{
   if (_error->PendingError() == true)
      return;

   /* Purpose of MMap::iSize:

      1. It is set by MMap::Map() to the size of the mmap'ed region
      (determined by the file size)
      and is used by ~MMap() to determine the size of the region to munmap.

      2. Additionally, in the course of work, DynamicMMap uses MMap::iSize
      to track the size of the region already occupied by allocations; i.e.,
      Base+iSize is the beginning of the free space.

      The two purposes are not obviously conflict-free with each
      other, therefore DynamicMMap() constructor needs to take special
      care to leave it in a state suitable for the second purpose
      (DynamicMMap's course of work), and ~DynamicMMap() destructor
      needs to do special tweaks to satisfy the expectations of ~MMap()
      according to the first purpose, in spite of iSize not reflecting
      the size of the whole mmap'ed region already at the end of work.

      DynamicMMap::WorkSpace is the size of the whole region available
      for new allocations and holding already allocated stuff. So, in
      this constructor, we must make WorkSpace at least as big as the
      already allocated and saved stuff in the file initially
      and at least as big as RequestedWorkSpace.
   */

   auto const EndOfFile = Fd->Size();

   /* The backing file must be made at least as big as the requested workspace
      that we are going to use in the course of work.
   */
   if (EndOfFile < RequestedWorkSpace)
   {
      if (!Fd->Truncate(RequestedWorkSpace))
         return;
   }

   if (!Map(F))
      return;

   /* Map(F) has set iSize to the size of the whole, possibly extended file */

   WorkSpace = iSize;

   /* Now decrease iSize back to only the already allocated stuff
      (that which has been saved in the file before extension)
      -- to satisfy the expectations of DynamicMMap methods.

      Note that at this moment Map(F) has ensured the file size fits size_t.
      (This note applies to the current size of the extended file as well as
      the initial size EndOfFile.)
   */
   static_assert(std::is_unsigned_v<decltype(EndOfFile)>,
                 "we want to rely that Fd::Size() is unsigned");
   iSize = static_cast<size_t>(EndOfFile);
}
									/*}}}*/
// DynamicMMap::DynamicMMap - Constructor for a non-file backed map	/*{{{*/
// ---------------------------------------------------------------------
/* This is just a fancy malloc really.. */
DynamicMMap::DynamicMMap(unsigned long const Flags,size_t const WorkSpace) :
             MMap(Flags | NoImmMap | UnMapped), Fd(0), WorkSpace(WorkSpace)
{
   if (_error->PendingError() == true)
      return;

   // allocate and zero memory
   Base = new unsigned char[WorkSpace]();
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

   size_t const EndOfFile = iSize;

   /* Prepare for Close(): iSize determines the region to be munmap'ed and
      therefore needs to be set to the whole file-backed workspace.
   */
   iSize = WorkSpace;

   Close(false);

   /* Finally, truncate the file to the region used for our actual allocations.
      (Not all of the initially mmap'ed workspace might have been used.)
    */
   Fd->Truncate(EndOfFile);
}
									/*}}}*/
// DynamicMMap::RawAllocate - Allocate a raw chunk of unaligned space	/*{{{*/
// ---------------------------------------------------------------------
/* This allocates a block of memory aligned to the given size */
std::optional<unsigned long> DynamicMMap::RawAllocate(size_t const Size,size_t const Aln)
{
   unsigned long Result = iSize;

   if (Aln != 0 && Result%Aln != 0)
   {
      size_t const Padding = Aln - (Result%Aln);
      // Just in case error check
      if (Padding > WorkSpace - Result)
      {
         _error->Error("Dynamic MMap ran out of room");
         return std::nullopt;
      }
      Result += Padding;
   }

   // Just in case error check
   if (Size > WorkSpace - Result)
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
std::optional<unsigned long> DynamicMMap::Allocate(size_t const ItemSize)
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
std::optional<unsigned long> DynamicMMap::WriteString(const char * const String,
				       size_t const Len)
{
   // Very improbable.
   // (Check: If std::string is always passed as arg, this never happens.)
   if (Len == SIZE_MAX)
   {
      _error->Error("Dynamic MMap allocation error: string too long");
      return std::nullopt;
   }

   const auto Result = RawAllocate(Len+1,0);
   if (Result)
   {
      char * const dest = static_cast<char *>(Base) + *Result;
      memcpy(dest,String,Len);
      dest[Len] = 0;
   }
   return Result;
}
