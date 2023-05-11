// Description								/*{{{*/
// $Id: hashes.cc,v 1.1 2002/07/23 17:54:51 niemeyer Exp $
/* ######################################################################

   Hashes - Simple wrapper around the hash functions

   This is just used to make building the methods simpler, this is the
   only interface required..

   ##################################################################### */
									/*}}}*/
// Include Files							/*{{{*/
#include <config.h>

#include <apt-pkg/hashes.h>
#include <apt-pkg/fileutl_opt.h>

#include <unistd.h>
#include <system.h>
									/*}}}*/
bool Hashes::Add(const void * const Data,std::size_t const Size)
{
   HashContainer::iterator I;
   for (I = HashSet.begin(); I != HashSet.end(); I++) {
      if (I->Add(Data,Size) == false)
         break;
   }
   return (I == HashSet.end());
}

// Hashes::AddF - Add a part of FileFd from the currpos to the checksum	/*{{{*/
// ---------------------------------------------------------------------
/* */
bool Hashes::AddF(FileFd &F,filesize const Size)
{
   return Consume(F,
                  [this](const void * const Buf, size_t const Count) -> bool
                  {
                     return Add(Buf,Count);
                  },
                  Size);
}
									/*}}}*/
// Hashes::AddFile - Add content of a whole file into the checksum	/*{{{*/
// ---------------------------------------------------------------------
/* */
bool Hashes::AddFile(const std::string &File)
{
   return ConsumeFile(File,
                      [this](const void * const Buf, size_t const Count) -> bool
                      {
                         return Add(Buf,Count);
                      });
}
									/*}}}*/
Hashes::Hashes()
{
   static const char * const htypes[] = {
      "MD5-Hash",
      "BLAKE2b",
      NULL
   };

   for (const char * const * name = htypes; *name != NULL; name++)
       HashSet.push_back(raptHash(*name));
}
