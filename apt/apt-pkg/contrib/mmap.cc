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
#include <apt-pkg/configuration.h>
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
MMap::MMap(FileFd &F,unsigned long Flags) : Flags(Flags), iSize(0),
                     Base(nullptr)
{
   if ((this->Flags & NoImmMap) != NoImmMap)
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
   iSize = Fd.Size();
   
   // Set the permissions.
   int Prot = PROT_READ;
   int Map = MAP_SHARED;
   if ((this->Flags & ReadOnly) != ReadOnly)
      Prot |= PROT_WRITE;
   if ((this->Flags & Public) != Public)
      Map = MAP_PRIVATE;
   
   if (iSize == 0)
      return _error->Error(_("Can't mmap an empty file"));
   
   // Map it.
   Base = mmap(0,iSize,Prot,Map,Fd.Fd(),0);
   if (Base == MAP_FAILED)
      return _error->Errno("mmap",_("Couldn't make mmap of %llu bytes"),iSize);

   return true;
}
									/*}}}*/
// MMap::Close - Close the map						/*{{{*/
// ---------------------------------------------------------------------
/* */
bool MMap::Close(bool DoSync)
{
   if ((this->Flags & UnMapped) == UnMapped || validData() == false || iSize == 0)
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
   if ((this->Flags & UnMapped) == UnMapped)
      return true;
   
#ifdef _POSIX_SYNCHRONIZED_IO   
   if ((this->Flags & ReadOnly) != ReadOnly)
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
   if ((this->Flags & UnMapped) == UnMapped)
      return true;
   
#ifdef _POSIX_SYNCHRONIZED_IO
   unsigned long PSize = sysconf(_SC_PAGESIZE);
   if ((this->Flags & ReadOnly) != ReadOnly)
      if (msync((char *)Base+(int)(Start/PSize)*PSize,Stop - Start,MS_SYNC) != 0)
	 return _error->Errno("msync","Unable to write mmap");
#endif   
   return true;
}
									/*}}}*/

// DynamicMMap::DynamicMMap - Constructor				/*{{{*/
// ---------------------------------------------------------------------
/* */
DynamicMMap::DynamicMMap(FileFd &F,unsigned long Flags,unsigned long long WorkSpace,
                         unsigned long long Grow, unsigned long long l_Limit) :
             MMap(F,Flags | NoImmMap), Fd(&F), WorkSpace(WorkSpace),
             GrowFactor(Grow), Limit(l_Limit)
{
   if (_error->PendingError() == true)
      return;

   // disable Moveable if we don't grow
   if (Grow == 0)
      this->Flags &= ~Moveable;

   unsigned long long EndOfFile = Fd->Size();
   if (EndOfFile > WorkSpace)
      WorkSpace = EndOfFile;
   else
   {
      Fd->Seek(WorkSpace);
      char C = 0;
      Fd->Write(&C,sizeof(C));
   }
   
   Map(F);
   iSize = EndOfFile;
}
									/*}}}*/
// DynamicMMap::DynamicMMap - Constructor for a non-file backed map	/*{{{*/
// ---------------------------------------------------------------------
/* This is just a fancy malloc really.. */
DynamicMMap::DynamicMMap(unsigned long Flags,unsigned long long WorkSpace,
                         unsigned long long Grow, unsigned long long l_Limit) :
             MMap(Flags | NoImmMap | UnMapped), Fd(0), WorkSpace(WorkSpace),
             GrowFactor(Grow), Limit(l_Limit)
{
   if (_error->PendingError() == true)
      return;

   // disable Moveable if we don't grow
   if (Grow == 0)
      this->Flags &= ~Moveable;

   Base = calloc(WorkSpace, 1);
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
      free(Base);
      return;
   }
   
   unsigned long long EndOfFile = iSize;
   iSize = WorkSpace;
   Close(false);
   ftruncate(Fd->Fd(),EndOfFile);
}  
									/*}}}*/
void DynamicMMap::DebugPrintPools(const char * const MsgPrefix)
{
   for (size_t i = 0; i < PoolCount; ++i)
   {
      _error->Warning(_("%sPool %zu, item size: %lu, start: %lu, count: %lu"),
                      MsgPrefix,
                      i,
                      Pools[i].ItemSize, Pools[i].Start, Pools[i].Count);
    }
}

// DynamicMMap::RawAllocate - Allocate a raw chunk of unaligned space	/*{{{*/
// ---------------------------------------------------------------------
/* This allocates a block of memory aligned to the given size */
std::experimental::optional<map_ptrloc> DynamicMMap::RawAllocate(unsigned long long Size,unsigned long Aln)
{
   unsigned long long Result = iSize;
   if (Aln != 0)
      Result += Aln - (iSize%Aln);

   // Just in case error check
   if (Result + Size > WorkSpace)
   {
      if (!Grow(Result + Size - WorkSpace))
      {
         _error->Error(_("Dynamic MMap ran out of room. Please increase the size "
                         "of APT::Cache-Start or APT::Cache-Limit, or remove value of APT::Cache-Limit. "
                         "Current values are: %llu, %llu. (man 5 apt.conf)"),
                       (unsigned long long) _config->FindI("APT::Cache-Start", 24*1024*1024),
                       (unsigned long long) _config->FindI("APT::Cache-Limit", 0));
         return std::experimental::optional<map_ptrloc>();
      }
   }

   iSize = Result + Size;

   return std::experimental::optional<map_ptrloc>(Result);
}
									/*}}}*/
// DynamicMMap::Allocate - Pooled aligned allocation			/*{{{*/
// ---------------------------------------------------------------------
/* This allocates an Item of size ItemSize so that it is aligned to its
   size in the file. */
std::experimental::optional<map_ptrloc> DynamicMMap::Allocate(unsigned long ItemSize)
{
   if (ItemSize == 0)
   {
      _error->Error("Can't allocate an item of size zero");
      return std::experimental::optional<map_ptrloc>();
   }

   // Look for a matching pool entry
   Pool *I;
   Pool *Empty = 0;
   for (I = Pools; I != Pools + PoolCount; ++I)
   {
      if (I->ItemSize == 0)
	 Empty = I;
      if (I->ItemSize == ItemSize)
	 break;
   }

   // No pool is allocated, use an unallocated one
   if (I == Pools + PoolCount)
   {
      static const bool debug_allocate = _config->FindB("Debug::DynamicMMap::Allocate", false);

      if (debug_allocate)
      {
         _error->Warning(_("DynamicMMap::Allocate: allocating item of size %lu"), ItemSize);

         DebugPrintPools("DynamicMMap::Allocate: ");
      }

      // Woops, we ran out, the calling code should allocate more.
      if (Empty == 0)
      {
	 _error->Error("Ran out of allocation pools");
	 return std::experimental::optional<map_ptrloc>();
      }
      
      I = Empty;
      I->ItemSize = ItemSize;
      I->Count = 0;
   }
   
   unsigned long Result = 0;
   // Out of space, allocate some more
   if (I->Count == 0)
   {
      const unsigned long size = 20*1024;
      I->Count = size/ItemSize;
      Pool* oldPools = Pools;
      auto idxResult = RawAllocate(I->Count*ItemSize,ItemSize);
      if (Pools != oldPools)
         I += Pools - oldPools;

      // Does the allocation failed ?
      if (!idxResult)
         return idxResult;

      Result = *idxResult;
      I->Start = Result;
   }
   else
      Result = I->Start;

   I->Count--;
   I->Start += ItemSize;  
   return std::experimental::optional<map_ptrloc>(Result/ItemSize);
}
									/*}}}*/
// DynamicMMap::WriteString - Write a string to the file		/*{{{*/
// ---------------------------------------------------------------------
/* Strings are not aligned to anything */
std::experimental::optional<map_ptrloc> DynamicMMap::WriteString(const char *String,
				       unsigned long Len)
{
   if (Len == std::numeric_limits<unsigned long>::max())
      Len = strlen(String);

   auto Result = RawAllocate(Len+1,0);

   if (Base == NULL || !Result)
      return std::experimental::optional<map_ptrloc>();

   memcpy((char *)Base + *Result,String,Len);
   ((char *)Base)[*Result + Len] = 0;

   return Result;
}
									/*}}}*/
// DynamicMMap::Grow - Grow the mmap					/*{{{*/
// ---------------------------------------------------------------------
/* This method is a wrapper around different methods to (try to) grow
   a mmap (or our char[]-fallback). Encounterable environments:
   1. mmap -> mremap with MREMAP_MAYMOVE
   2. alloc -> realloc */
bool DynamicMMap::Grow(unsigned long long size)
{
   if (GrowFactor <= 0)
      return _error->Error(_("Unable to increase size of the MMap as automatic growing is disabled by user."));

   static const bool debug_grow = _config->FindB("Debug::DynamicMMap::Grow", false);

   unsigned long long grow_size = 0;

   do
   {
      grow_size += GrowFactor;

      if (Limit != 0 && WorkSpace + grow_size > Limit)
         return _error->Error(_("Unable to increase the size of the MMap as the "
                                "limit of %llu bytes is already reached."), Limit);
   } while (grow_size < size);

   unsigned long long const newSize = WorkSpace + grow_size;

   if (Fd != 0)
   {
      Fd->Seek(newSize - 1);
      char C = 0;
      Fd->Write(&C,sizeof(C));
   }

   if (debug_grow)
      DebugPrintPools("DynamicMMap::Grow: before remapping: ");

   std::ptrdiff_t const poolOffset = reinterpret_cast<char *>(Pools) - static_cast<char *>(Base);

   if (Fd != 0)
   {
      void *tmp_base = MAP_FAILED;
#ifdef MREMAP_MAYMOVE
      if ((this->Flags & Moveable) == Moveable)
         tmp_base = mremap(Base, WorkSpace, newSize, MREMAP_MAYMOVE);
      else
#endif
         tmp_base = mremap(Base, WorkSpace, newSize, 0);

      if (debug_grow)
         _error->Warning(_("DynamicMMap::Grow: mremap from %llu to %llu, result: %s"), WorkSpace, newSize, (tmp_base == MAP_FAILED) ? _("Fail") : _("Success"));

      if (tmp_base == MAP_FAILED)
         return false;

      Base = tmp_base;
   } else {
      if ((this->Flags & Moveable) != Moveable)
         return false;

      void *tmp_base = realloc(Base, newSize);

      if (debug_grow)
         _error->Warning(_("DynamicMMap::Grow: realloc from %llu to %llu, result: %s"), WorkSpace, newSize, (tmp_base == nullptr) ? _("Fail") : _("Success"));

      if (tmp_base == NULL)
         return false;

      Base = tmp_base;

      /* Set new memory to 0 */
      memset((char*)Base + WorkSpace, 0, newSize - WorkSpace);
   }

   Pools = reinterpret_cast<decltype(Pools)>(static_cast<char *>(Base) + poolOffset);
   WorkSpace = newSize;

   if (debug_grow)
      DebugPrintPools("DynamicMMap::Grow: after remapping: ");

   return true;
}
									/*}}}*/
