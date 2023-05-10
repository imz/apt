// Include Files
#include "config.h"

#include <apt-pkg/rhash.h>
#include <apt-pkg/strutl.h>
#include <apt-pkg/fileutl_opt.h>

#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <config.h>
#include <system.h>

raptHash::raptHash(const string & HashName) : HashCtx(NULL), HashType(HashName)
{
   pgpHashAlgo algo;
   if (HashType == "SHA512-Hash")
      algo = PGPHASHALGO_SHA512;
   else if (HashType == "SHA384-Hash")
      algo = PGPHASHALGO_SHA384;
   else if (HashType == "SHA256-Hash")
      algo = PGPHASHALGO_SHA256;
   else if (HashType == "SHA1-Hash")
      algo = PGPHASHALGO_SHA1;
   else if (HashType == "MD5-Hash")
      algo = PGPHASHALGO_MD5;
   else if (HashType == "BLAKE2b")
      algo = PGPHASHALGO_BLAKE2B;
   else
      /* HashCtx remains NULL as an indicator that this object is invalid */
      return;

   HashCtx = rpmDigestInit(algo, RPMDIGEST_NONE);
}

raptHash::raptHash(const raptHash & Hash)
{
   HashCtx = rpmDigestDup(Hash.HashCtx);
   Value = Hash.Value;
   HashType = Hash.HashType;
}

raptHash::~raptHash()
{
   if (HashCtx)
      rpmDigestFinal(HashCtx, NULL, NULL, 0);
}

raptHash & raptHash::operator= (const raptHash & Hash)
{
   if (this == &Hash)
      return *this;

   if (HashCtx)
      rpmDigestFinal(HashCtx, NULL, NULL, 0);

   HashCtx = rpmDigestDup(Hash.HashCtx);
   Value = Hash.Value;
   HashType = Hash.HashType;
   return *this;
}

// raptHash::Result - Return checksum value                        /*{{{*/
// ---------------------------------------------------------------------
/* Add() may not be called after this */
string raptHash::Result()
{
   void *data = NULL;
   size_t dlen = 0;

   rpmDigestFinal(HashCtx, &data, &dlen, 1);
   if (data) {
      Value = string((const char*)data);
      free(data);
   }
   HashCtx = NULL;

   return Value;
}
									/*}}}*/
// raptHash::Add - Adds content of buffer into the checksum        /*{{{*/
// ---------------------------------------------------------------------
/* May not be called after Result() is called */
bool raptHash::Add(const void * const data,std::size_t const len)
{
   int rc;
   if (HashCtx == NULL)
      return false;

   rc = rpmDigestUpdate(HashCtx, data, len);

   return (rc == 0);
}
									/*}}}*/
// raptHash::AddFile - Add content of a whole file into the checksum	/*{{{*/
// ---------------------------------------------------------------------
/* */
bool raptHash::AddFile(const std::string &File)
{
   return ConsumeFile(File,
                      [this](const void * const Buf, size_t const Count) -> bool
                      {
                         return Add(Buf,Count);
                      });
}
									/*}}}*/
