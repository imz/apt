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
									/*}}}*/

// MMap::MMap - Constructor						/*{{{*/
// ---------------------------------------------------------------------
/* */
MMap::MMap(FileFd &F,unsigned long const Flags) : Flags(Flags), iSize(0),
                     Base(0)
{
   if ((Flags & NoImmMap) != NoImmMap)
      Map(F);
}
									/*}}}*/
// MMap::MMap - Constructor						/*{{{*/
// ---------------------------------------------------------------------
/* */
MMap::MMap(unsigned long const Flags) : Flags(Flags), iSize(0),
                     Base(0)
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
   iSize = Fd.Size();

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
   if (Base == (void *)-1)
      return _error->Errno("mmap",_("Couldn't make mmap of %lu bytes"),iSize);

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
                         unsigned long const RequestedWorkSpace) :
   MMap(F,Flags | NoImmMap), /* NoImmMap to invoke MMap::Map() ourselves */
   Fd(&F),
   WorkSpace(RequestedWorkSpace)
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
      for new allocations and holding already allocated stuff.
   */

   unsigned long EndOfFile = Fd->Size();
   if (EndOfFile > WorkSpace)
      /* The already allocated and saved stuff exceeds the requested workspace,
         so workspace must be increased.
      */
      WorkSpace = EndOfFile;
   else
   {
      /* The backing file must be made at least as big as the workspace
         that we are going to use in the course of work.
      */
      Fd->Seek(WorkSpace);
      char C = 0;
      Fd->Write(&C,sizeof(C));
   }

   Map(F); /* sets iSize to the size of the whole, possibly extended file */

   /* Now decrease iSize back to only the already allocated stuff
      (that which has been saved in the file before extension)
      -- to satisfy the expectations of DynamicMMap methods.
   */
   iSize = EndOfFile;
}
									/*}}}*/
// DynamicMMap::DynamicMMap - Constructor for a non-file backed map	/*{{{*/
// ---------------------------------------------------------------------
/* This is just a fancy malloc really.. */
DynamicMMap::DynamicMMap(unsigned long const Flags,unsigned long const WorkSpace) :
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

   unsigned long EndOfFile = iSize;

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
unsigned long DynamicMMap::RawAllocate(unsigned long const Size,unsigned long const Aln)
{
   unsigned long Result = iSize;
   if (Aln != 0)
      Result += Aln - (iSize%Aln);

   // Just in case error check
   if (Result + Size > WorkSpace)
   {
      _error->Error("Dynamic MMap ran out of room");
      return 0;
   }

   iSize = Result + Size;

   return Result;
}
									/*}}}*/
// DynamicMMap::Allocate - Pooled aligned allocation			/*{{{*/
// ---------------------------------------------------------------------
/* This allocates an Item of size ItemSize so that it is aligned to its
   size in the file. */
unsigned long DynamicMMap::Allocate(unsigned long const ItemSize)
{
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
	 return 0;
      }

      I = Empty;
      I->ItemSize = ItemSize;
      I->Count = 0;
   }

   // Out of space, allocate some more
   if (I->Count == 0)
   {
      I->Count = 20*1024/ItemSize;
      I->Start = RawAllocate(I->Count*ItemSize,ItemSize);
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
unsigned long DynamicMMap::WriteString(const char * const String,
				       unsigned long Len)
{
   if (Len == (unsigned long)-1)
      Len = strlen(String);

   unsigned long Result = iSize;
   // Just in case error check
   if (Result + Len > WorkSpace)
   {
      _error->Error("Dynamic MMap ran out of room");
      return 0;
   }

   iSize += Len + 1;
   memcpy((char *)Base + Result,String,Len);
   ((char *)Base)[Result + Len] = 0;
   return Result;
}
									/*}}}*/
