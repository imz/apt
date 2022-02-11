// Description								/*{{{*/
// $Id: pkgrecords.h,v 1.2 2002/11/28 18:54:15 niemeyer Exp $
/* ######################################################################

   Package Records - Allows access to complete package description records
                     directly from the file.

   The package record system abstracts the actual parsing of the
   package files. This is different than the generators parser in that
   it is used to access information not generate information. No
   information touched by the generator should be parable from here as
   it can always be retreived directly from the cache.

   ##################################################################### */
									/*}}}*/
#ifndef PKGLIB_PKGRECORDS_H
#define PKGLIB_PKGRECORDS_H

#include <apt-pkg/pkgcache.h>
#include <apt-pkg/fileutl.h>

#include <vector>

class pkgRecords
{
   public:
   class Parser;

   private:

   pkgCache &Cache;
   std::vector<Parser *>Files;

   public:

   // Lookup function
   Parser &Lookup(pkgCache::VerFileIterator const &Ver);

   // Construct destruct
   pkgRecords(pkgCache &Cache);
   ~pkgRecords();
};

class pkgRecords::Parser
{
   protected:

   virtual bool Jump(pkgCache::VerFileIterator const &Ver) = 0;

   public:
   friend class pkgRecords;

   // These refer to the archive file for the Version
   virtual string FileName() = 0;
   virtual string MD5Hash() = 0;
   virtual string BLAKE2b() = 0;
   virtual string SourcePkg() = 0;

   // These are some general stats about the package
   virtual string Maintainer() = 0;
   virtual string ShortDesc() = 0;
   virtual string LongDesc() = 0;
   virtual string Name() = 0;
   virtual string Changelog() = 0;

   // These are not supported by all repository types and can fail
   virtual bool FileList(std::vector<string> &Files) = 0;

   // The record in binary form
   virtual void GetRec(const char *&Start,const char *&Stop) = 0;

   // CNC:2003-11-21
   virtual bool HasFile(const char *File) = 0;

   virtual ~Parser() {}
};

#endif
