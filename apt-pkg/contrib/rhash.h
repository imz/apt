// Description								/*{{{*/
/* ######################################################################

   This is a C++ interface to rpm's hash (aka digest) functions.

   ##################################################################### */
									/*}}}*/
#ifndef APTPKG_RHASH_H
#define APTPKG_RHASH_H

#include <apt-pkg/fileutl.h>

#include <string>
#include <rpm/rpmpgp.h>

using std::string;

class raptHash
{
   DIGEST_CTX HashCtx;
   string Value;
   string HashType;

   public:

   [[nodiscard]] bool Add(const void *inbuf,std::size_t inlen);
   bool Add(const char * const Data) {return Add(Data,strlen(Data));}
   [[nodiscard]] bool AddFile(const std::string &File);
   [[nodiscard]] bool Add(const unsigned char *Beg,const unsigned char *End)
                  {return Add(Beg,End-Beg);}
   string Result();
   string Type() {return HashType;};

   raptHash(const string & HashName);
   raptHash(const raptHash & Hash);
   raptHash & operator= (const raptHash & Hash);
   ~raptHash();
};

#endif
