// Description								/*{{{*/
// $Id: rpmsrcrecords.h,v 1.5 2002/08/08 20:07:33 niemeyer Exp $
/* ######################################################################

   SRPM Records - Parser implementation for RPM style source indexes

   #####################################################################
 */
									/*}}}*/
#ifndef PKGLIB_RPMSRCRECORDS_H
#define PKGLIB_RPMSRCRECORDS_H

#include <apt-pkg/srcrecords.h>
#include <apt-pkg/fileutl.h>
#include <rpm/rpmlib.h>


class RPMHandler;

class rpmSrcRecordParser : public pkgSrcRecords::Parser
{
   RPMHandler *Handler;
   Header HeaderP;

   const char *StaticBinList[400];

   char *Buffer;
   unsigned int BufSize;
   unsigned int BufUsed;

   void BufCat(const char *text);
   void BufCat(const char *begin, const char *end);
   void BufCatTag(const char *tag, const char *value);
   void BufCatDep(const char *pkg, const char *version, int flags);
   void BufCatDescr(const char *descr);

public:
   virtual bool Restart() override;
   virtual bool Step() override;
   virtual bool Jump(unsigned long Off) override;

   virtual string FileName() const override;

   virtual string Package() const override;
   virtual string Version() const override;
   virtual string Maintainer() const override;
   virtual string Section() const override;
   virtual string Changelog() const override;
   virtual const char **Binaries() override;
   virtual unsigned long Offset() override;
   virtual string AsStr() override;
   virtual bool Files(vector<pkgSrcRecords::File> &F) override;
   virtual bool BuildDepends(vector<BuildDepRec> &BuildDeps, bool ArchOnly) override;

   rpmSrcRecordParser(string File,pkgIndexFile const *Index);
   ~rpmSrcRecordParser();
};

#endif
