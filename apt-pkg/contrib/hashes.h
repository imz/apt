// Description								/*{{{*/
// $Id: hashes.h,v 1.1 2002/07/23 17:54:51 niemeyer Exp $
/* ######################################################################

   Hashes - Simple wrapper around the hash functions

   This is just used to make building the methods simpler, this is the
   only interface required..

   ##################################################################### */
									/*}}}*/
#ifndef APTPKG_HASHES_H
#define APTPKG_HASHES_H

#include <vector>
#include <apt-pkg/rhash.h>

typedef std::vector<raptHash> HashContainer;

class Hashes
{
   public:

   HashContainer HashSet;

   bool Add(const void *Data,std::size_t Size);
   bool Add(const char * const Data) {return Add(Data,strlen(Data));}
   bool AddFD(int Fd,unsigned long Size);
   [[nodiscard]] bool AddF(FileFd &F,filesize Size);
   bool AddFile(const std::string &File);
   inline bool Add(const unsigned char *Beg,const unsigned char *End)
                  {return Add(Beg,End-Beg);}

   Hashes();
};

#endif
