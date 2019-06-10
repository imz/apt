// Description								/*{{{*/
// $Id: rpmrecords.h,v 1.3 2002/08/08 20:07:33 niemeyer Exp $
/* ######################################################################

   RPM Package Records - Parser for RPM hdlist/rpmdb files

   This provides display-type parsing for the Packages file. This is
   different than the the list parser which provides cache generation
   services. There should be no overlap between these two.

   ##################################################################### */
									/*}}}*/
// Header section: pkglib
#ifndef PKGLIB_RPMRECORDS_H
#define PKGLIB_RPMRECORDS_H

#include <apt-pkg/pkgrecords.h>
#include <apt-pkg/fileutl.h>
#include <rpm/rpmlib.h>
#include <cinttypes>


class RPMHandler;

class rpmRecordParser : public pkgRecords::Parser
{
   RPMHandler *Handler;
   bool IsDatabase;

   Header HeaderP;

   char *Buffer;
   unsigned BufSize;
   unsigned BufUsed;

   void BufCat(const char *text);
   void BufCat(const char *begin, const char *end);
   void BufCatTag(const char *tag, const char *value);
   void BufCatDep(const char *pkg, const char *version, uint32_t flags);
   void BufCatDescr(const char *descr);

   protected:

   virtual bool Jump(pkgCache::VerFileIterator const &Ver) override;

   public:

   // These refer to the archive file for the Version
   virtual string FileName() override;
   virtual string MD5Hash() override;
   virtual string SourcePkg() override;

   // These are some general stats about the package
   virtual string Maintainer() override;
   virtual string ShortDesc() override;
   virtual string LongDesc() override;
   virtual string Name() override;
   virtual string Changelog() override;

   inline Header GetRecord() { return HeaderP; };

   // The record in raw text, in standard Debian format
   virtual void GetRec(const char *&Start,const char *&Stop) override;

   virtual bool HasFile(const char *File) override;

   rpmRecordParser(string File,pkgCache &Cache);
   ~rpmRecordParser();
};


#endif
